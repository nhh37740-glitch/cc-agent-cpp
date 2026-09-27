pipeline {
    agent { label 'edge-windows' }
    options { timestamps(); disableConcurrentBuilds() }
    stages {
        stage('Check module map') {
            steps { bat 'python scripts\\check_module_boundaries.py' }
        }
        stage('Build and test') {
            steps {
                bat '''
                    git submodule update --init --recursive
                    if errorlevel 1 exit /b %errorlevel%
                    cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
                    if errorlevel 1 exit /b %errorlevel%
                    cmake --build build --config Release --parallel 2
                    if errorlevel 1 exit /b %errorlevel%
                    ctest --test-dir build -C Release --output-on-failure
                    if errorlevel 1 exit /b %errorlevel%
                '''
            }
        }
        stage('Binary package') {
            steps {
                powershell 'scripts/package-windows-release.ps1 -Configuration Release'
                archiveArtifacts artifacts: 'dist/*.zip,dist/*.sha256,dist/manifest.json', fingerprint: true
            }
        }
        stage('Windows runtime image') {
            steps {
                bat 'docker build -f Dockerfile.windows.runtime -t cc-agent-cpp:%BUILD_NUMBER% .'
            }
        }
    }
}
