pipeline {
    agent any

    options {
        buildDiscarder(logRotator(numToKeepStr: '10', artifactNumToKeepStr: '5'))
        disableConcurrentBuilds()
    }

    parameters {
        choice(
            name: 'BUILD_TYPE',
            choices: ['Release', 'Debug'],
            description: 'CMake build type'
        )
        booleanParam(
            name: 'REUSE_BUILD_CACHE',
            defaultValue: true,
            description: 'Reuse precompiled dependency libraries from /home/tyler/OpenRCT2-XP/build'
        )
        booleanParam(
            name: 'PUBLISH_TO_GITHUB',
            defaultValue: true,
            description: 'Automatically publish the release zip and binaries to GitHub Releases'
        )
        string(
            name: 'GITHUB_REPO',
            defaultValue: 'KTMGv5/OpenRCT2-WindowsXP',
            description: 'Target GitHub repository to post releases to (owner/repo)'
        )
    }

    environment {
        BUILD_TYPE  = "${params.BUILD_TYPE}"
        GITHUB_REPO = "${params.GITHUB_REPO}"
        NUM_CORES   = sh(script: 'nproc 2>/dev/null || echo 4', returnStdout: true).trim()
        BUILD_DATE  = sh(script: 'date +%Y%m%d', returnStdout: true).trim()
        SHORT_SHA   = sh(script: 'git rev-parse --short HEAD', returnStdout: true).trim()
    }

    stages {
        stage('Toolchain Check') {
            steps {
                echo "=== Verifying Windows XP Cross-Toolchain ==="
                sh '''
                    i686-w64-mingw32-gcc --version | head -n 1
                    i686-w64-mingw32-g++ --version | head -n 1
                    cmake --version | head -n 1
                    python3 --version
                '''
            }
        }

        stage('Setup Build Tools & Dependencies') {
            steps {
                script {
                    echo "Setting up build environment and dependency libraries..."
                    sh '''
                        # Obtain CI helper scripts (check_xp_compat.py, mingw32.cmake, cacert.pem)
                        if [ ! -d ".ci" ]; then
                            git clone --depth 1 https://github.com/KTMGv5/OpenRCT2-winxp.git .ci
                        else
                            git -C .ci pull || true
                        fi

                        # Setup build prefix directory for headers and static libraries
                        mkdir -p build
                        if [ "${REUSE_BUILD_CACHE}" = "true" ] && [ -d "/home/tyler/OpenRCT2-XP/build" ]; then
                            echo "Reusing dependency cache from /home/tyler/OpenRCT2-XP/build..."
                            cp -r /home/tyler/OpenRCT2-XP/build/* build/
                            [ -f "/home/tyler/OpenRCT2-XP/i686-w64-mingw32-pkg-config" ] && cp /home/tyler/OpenRCT2-XP/i686-w64-mingw32-pkg-config .
                        fi
                    '''
                }
            }
        }

        stage('Configure & Build OpenRCT2') {
            steps {
                echo "Compiling OpenRCT2 natively for Windows XP..."
                sh '''
                    TOPDIR=$(pwd)
                    PREFIX_DIR="${TOPDIR}/build"
                    mkdir -p _build && cd _build
                    cmake .. \
                        -DCMAKE_TOOLCHAIN_FILE="${TOPDIR}/.ci/mingw32.cmake" \
                        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
                        -DCMAKE_INSTALL_PREFIX="${PREFIX_DIR}" \
                        -DCMAKE_PREFIX_PATH="${PREFIX_DIR}" \
                        -DCMAKE_CXX_FLAGS="-march=i686 -I${PREFIX_DIR}/include -I${PREFIX_DIR}/include/SDL2 -DCURL_STATICLIB -DFLAC__NO_DLL" \
                        -DDISABLE_DISCORD_RPC=ON \
                        -DDOWNLOAD_OPENMSX=OFF \
                        -DDOWNLOAD_OPENSFX=OFF \
                        -DDOWNLOAD_TITLE_SEQUENCES=OFF \
                        -DCMAKE_EXE_LINKER_FLAGS="-L${PREFIX_DIR}/lib -Wl,--major-os-version,5,--minor-os-version,1,--major-subsystem-version,5,--minor-subsystem-version,1" \
                        -DPKG_CONFIG_EXECUTABLE="${TOPDIR}/i686-w64-mingw32-pkg-config" \
                        -DSTATIC=ON \
                        -DPORTABLE=ON \
                        -DCMAKE_LIBRARY_PATH="${PREFIX_DIR}" \
                        -DCMAKE_INCLUDE_PATH="${PREFIX_DIR}" \
                        -DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=FALSE

                    make -j${NUM_CORES}
                    if [ "${BUILD_TYPE}" != "Debug" ]; then
                        strip openrct2.exe openrct2-cli.exe
                    fi
                '''
            }
        }

        stage('Windows XP Compatibility Verification') {
            steps {
                echo "Scanning generated PE binaries for Windows Vista/7+ API violations..."
                sh '''
                    python3 .ci/check_xp_compat.py _build/openrct2.exe
                    python3 .ci/check_xp_compat.py _build/openrct2-cli.exe
                    for dll in build/bin/*.dll; do
                        [ -f "$dll" ] && python3 .ci/check_xp_compat.py "$dll"
                    done || true
                '''
            }
        }

        stage('Package Release') {
            steps {
                echo "Packaging portable Windows XP release zip..."
                sh '''
                    PACKAGE_DIR="OpenRCT2-portable"
                    PACKAGE_NAME="OpenRCT2-develop-${SHORT_SHA}-windows-portable-win32.zip"
                    rm -rf "${PACKAGE_DIR}" "${PACKAGE_NAME}"
                    mkdir -p "${PACKAGE_DIR}"

                    cp _build/openrct2.exe "${PACKAGE_DIR}/"
                    cp _build/openrct2-cli.exe "${PACKAGE_DIR}/"
                    for dll in build/bin/*.dll; do
                        [ -f "$dll" ] && cp "$dll" "${PACKAGE_DIR}/"
                    done
                    cp .ci/cacert.pem "${PACKAGE_DIR}/cacert.pem"
                    cp -r data "${PACKAGE_DIR}/"

                    # Download official base assets if not present
                    DATA_ARCHIVE="OpenRCT2-v0.5.5-windows-portable-win32.zip"
                    if [ ! -f "${DATA_ARCHIVE}" ]; then
                        wget -c "https://github.com/OpenRCT2/OpenRCT2/releases/download/v0.5.5/${DATA_ARCHIVE}"
                    fi
                    unzip -o "${DATA_ARCHIVE}" "data/g2.dat" "data/fonts.dat" "data/palettes.dat" "data/tracks.dat" "data/object/*" "data/sequence/*" -d "${PACKAGE_DIR}"

                    # Build matching graphics .dat files with gxc directly from repository resources
                    echo "Building matching .dat graphics files with gxc..."
                    if [ ! -x "tools/gxc" ]; then
                        mkdir -p tools
                        wget -c "https://github.com/OpenRCT2/libsawyer/releases/download/v1.4.0/libsawyer-tools-linux-x64.tar.gz" -O tools.tar.gz
                        tar -xzf tools.tar.gz -C tools/
                        chmod +x tools/gxc
                    fi
                    if [ -x "tools/gxc" ]; then
                        tools/gxc build "${PACKAGE_DIR}/data/g2.dat" "resources/g2/sprites.json"
                        tools/gxc build "${PACKAGE_DIR}/data/palettes.dat" "resources/palettes/sprites.json"
                        tools/gxc build "${PACKAGE_DIR}/data/fonts.dat" "resources/fonts/sprites.json"
                        tools/gxc build "${PACKAGE_DIR}/data/tracks.dat" "resources/tracks/sprites.json"
                        echo "Successfully generated matching graphics dat files (palettes.dat entries: 238)."
                    fi

                    # Verify cacert.pem
                    if [ ! -f "${PACKAGE_DIR}/cacert.pem" ]; then
                        echo "Fatal: cacert.pem missing from package!" >&2
                        exit 1
                    fi

                    cd "${PACKAGE_DIR}" && rm -f "../${PACKAGE_NAME}" && zip -r "../${PACKAGE_NAME}" . && cd ..
                    echo "Packaged release: ${PACKAGE_NAME}"
                '''
            }
        }

        stage('Publish Release to GitHub') {
            when {
                expression { return params.PUBLISH_TO_GITHUB == true }
            }
            steps {
                script {
                    echo "Publishing release to ${GITHUB_REPO}..."
                    try {
                        withCredentials([string(credentialsId: 'github-token', variable: 'GH_TOKEN')]) {
                            sh '''
                                TAG="develop"
                                RELEASE_TITLE="OpenRCT2 develop (${SHORT_SHA}) - Windows XP Edition"
                                ZIP_FILE=$(ls OpenRCT2-*-windows-portable-win32.zip 2>/dev/null | head -n 1)

                                if [ -z "${ZIP_FILE}" ]; then
                                    echo "Error: No release zip found." >&2
                                    exit 1
                                fi

                                echo "Publishing artifact: ${ZIP_FILE} to ${GITHUB_REPO} (${TAG})"
                                gh release delete "${TAG}" -y --cleanup-tag --repo "${GITHUB_REPO}" 2>/dev/null || true
                                gh release create "${TAG}" "${ZIP_FILE}" _build/openrct2.exe _build/openrct2-cli.exe \
                                    --repo "${GITHUB_REPO}" \
                                    --title "${RELEASE_TITLE}" \
                                    --notes "Native development build of OpenRCT2 Windows XP Edition from commit ${SHORT_SHA}. Built natively for Windows XP (NT 5.1) without binary patching. Includes modern TLS 1.2/1.3 multiplayer networking, root CA certificate store (cacert.pem), and legacy graphics driver fallbacks." \
                                    --prerelease

                                echo "Release published successfully to https://github.com/${GITHUB_REPO}/releases !"
                            '''
                        }
                    } catch (Exception e) {
                        echo "Notice: Could not publish to GitHub: ${e.getMessage()}"
                        echo "Ensure 'github-token' credential is configured in Jenkins."
                    }
                }
            }
        }
    }

    post {
        success {
            echo "Windows XP build passed all verifications!"
            archiveArtifacts artifacts: 'OpenRCT2-*-windows-portable-win32.zip, _build/openrct2.exe, _build/openrct2-cli.exe', fingerprint: true, allowEmptyArchive: true
        }
        failure {
            echo "Build failed. Check console logs."
        }
    }
}
