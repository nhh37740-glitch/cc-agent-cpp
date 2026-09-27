pipeline {
    agent { label 'media-workspace-agent' }
    options {
        timestamps()
        disableConcurrentBuilds()
        timeout(time: 180, unit: 'MINUTES')
        buildDiscarder(logRotator(numToKeepStr: '20'))
    }
    stages {
        stage('Checkout') {
            steps { checkout scm }
        }
        stage('Initialize pinned llama.cpp') {
            steps {
                sh '''
                    set -eu
                    git submodule sync --recursive
                    git submodule update --init --recursive
                    expected=c1d0e7a004015f23bc0233470b747b596f29b264
                    actual="$(git -C third_party/llama.cpp rev-parse HEAD)"
                    test "$actual" = "$expected"
                    test -f third_party/llama.cpp/tools/mtmd/mtmd.h
                    test -f third_party/llama.cpp/LICENSE
                    printf 'llama.cpp %s ready\\n' "$actual"
                '''
            }
        }
        stage('Static module boundary check') {
            steps { sh 'python3 scripts/check_module_boundaries.py' }
        }
        stage('Linux build, tests, and module package') {
            steps {
                sh '''
                    set -eu
                    source_commit="$(git rev-parse HEAD)"
                    if [ -z "$(git status --porcelain --untracked-files=no)" ]; then
                      source_tree=clean
                    else
                      source_tree=dirty
                    fi
                    project_version="$(tr -d '\\r\\n' < VERSION)"
                    package_version="${project_version}-${BUILD_NUMBER}"
                    build_tag="cc-agent-cpp-build:${BUILD_NUMBER}"
                    sudo docker build \\
                      --file Dockerfile.linux \\
                      --target build \\
                      --build-arg BUILD_JOBS=1 \\
                      --build-arg PACKAGE_VERSION="$package_version" \\
                      --build-arg SOURCE_COMMIT="$source_commit" \\
                      --build-arg SOURCE_TREE="$source_tree" \\
                      --tag "$build_tag" .
                    container="$(sudo docker create --entrypoint /bin/true "$build_tag")"
                    cleanup() { sudo docker rm -f "$container" >/dev/null 2>&1 || true; }
                    trap cleanup EXIT
                    mkdir -p dist
                    sudo docker cp "$container:/src/dist/." dist/
                    cleanup
                    trap - EXIT
                    ls -l dist
                    (cd dist && sha256sum -c "cc-agent-cpp-${package_version}-linux-x86_64.tar.gz.sha256")
                '''
                archiveArtifacts artifacts: "dist/cc-agent-cpp-*-${BUILD_NUMBER}-linux-x86_64.tar.gz,dist/cc-agent-cpp-*-${BUILD_NUMBER}-linux-x86_64.tar.gz.sha256,dist/manifest-linux-x86_64.json", fingerprint: true
            }
        }
        stage('Linux runtime image and CLI smoke check') {
            steps {
                sh '''
                    set -eu
                    source_commit="$(git rev-parse HEAD)"
                    if [ -z "$(git status --porcelain --untracked-files=no)" ]; then
                      source_tree=clean
                    else
                      source_tree=dirty
                    fi
                    project_version="$(tr -d '\\r\\n' < VERSION)"
                    package_version="${project_version}-${BUILD_NUMBER}"
                    image="cc-agent-cpp:${BUILD_NUMBER}"
                    sudo docker build \\
                      --file Dockerfile.linux \\
                      --build-arg BUILD_JOBS=1 \\
                      --build-arg PACKAGE_VERSION="$package_version" \\
                      --build-arg SOURCE_COMMIT="$source_commit" \\
                      --build-arg SOURCE_TREE="$source_tree" \\
                      --tag "$image" .
                    sudo docker run --rm "$image" --help >/dev/null
                    sudo docker image inspect --format '{{json .Config.Healthcheck.Test}}' "$image"
                    printf 'Built %s; model inference is not started by CI.\\n' "$image"
                '''
            }
        }
    }
    post {
        always { echo "cc-agent-cpp Linux build ${env.BUILD_NUMBER}: ${currentBuild.currentResult}" }
    }
}
