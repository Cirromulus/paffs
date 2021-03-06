pipeline {
    agent {
        label 'x86_64'
    }
    post {
        failure {
            updateGitlabCommitStatus name: 'build', state: 'failed'
        }
        success {
            updateGitlabCommitStatus name: 'build', state: 'success'
        }
    }
    options {
        gitLabConnection('AVS GitLab')
        buildDiscarder(logRotator(numToKeepStr:'10'))
        skipDefaultCheckout()
    }
    triggers {
        gitlab(triggerOnPush: true,
               triggerOnMergeRequest: true,
               triggerOpenMergeRequestOnPush: 'both',
               branchFilterType: 'All')
        pollSCM('H/10 * * * *')
    }

    stages {
        stage('checkout') {
            steps {
                dir('paffs') {
                    // https://hbryavsci1l.hb.dlr.de:8929/avionics-software-open/paffs.git
                    checkout scm
                }
                dir('scons-build-tools') {
                    git credentialsId: 'd895b75a-06cc-4446-a936-afe31d36d02b',
                        url: 'https://hbryavsci1l.hb.dlr.de:8929/avionics-software-open/scons-build-tools.git'
                }
                dir('satfon-simulation') {
                    git credentialsId: 'd895b75a-06cc-4446-a936-afe31d36d02b',
                        url: 'https://hbryavsci1l.hb.dlr.de:8929/avionics-software-open/satfon-simulation.git'
                }
                dir('outpost-core') {
                    checkout([
                        $class: 'GitSCM',
                        branches: [[name: '*/master']],
                        doGenerateSubmoduleConfigurations: false,
                        extensions: [
                            [$class: 'SubmoduleOption', parentCredentials: true, recursiveSubmodules: true]
                        ],
                        submoduleCfg: [],
                        userRemoteConfigs: [[
                            credentialsId: 'jenkins-ssh-gitlab',
                            url: 'ssh://git@hbryavsci1l.hb.dlr.de:10022/avionics-software-open/outpost-core.git'
                        ]]
                    ])
                }
                stash includes: 'paffs/**', name: 'paffs'
                stash includes: 'scons-build-tools/**', name: 'scons-build-tools'
                stash includes: 'satfon-simulation/**', name: 'satfon-simulation'
                stash includes: 'outpost-core/**', name: 'outpost-core'
            }
        }
        stage("build-unit") {
            steps {
                dir('paffs') {
                    sh 'make test-unit'
                }
            }
        }
        stage("build-integration") {
            steps {
                dir('paffs') {
                    sh 'make test-integration'
                }
            }
        }
        stage("collect-testresults") {
            steps {
            dir('paffs') {
            step([$class: 'XUnitPublisher',
                        testTimeMargin: '3000',
                        thresholdMode: 1,
                        thresholds: [
                            [
                                $class: 'FailedThreshold',
                                failureNewThreshold: '',
                                failureThreshold: '0',
                                unstableNewThreshold: '',
                                unstableThreshold: ''
                            ],
                            [
                                $class: 'SkippedThreshold',
                                failureNewThreshold: '',
                                failureThreshold: '',
                                unstableNewThreshold: '',
                                unstableThreshold: '0']
                            ],
                            tools: [[
                                $class: 'GoogleTestType',
                                deleteOutputFiles: true,
                                failIfNotNew: true,
                                pattern: 'build/release/test/*.xml',
                                skipNoTestFiles: false,
                                stopProcessingIfError: true]]])
                }
            }
        }
        stage("build-embedded-officemodel-1") {
            agent {
                label 'sparc-rtems'
            }
            steps {
                unstash 'paffs'
                unstash 'scons-build-tools'
                unstash 'satfon-simulation'
                unstash 'outpost-core'
                
                dir('paffs') {
                    sh 'make build-embedded-om1'
                }
            }
        }
                stage("build-embedded-officemodel-2") {
            agent {
                label 'sparc-rtems'
            }
            steps {
                unstash 'paffs'
                unstash 'scons-build-tools'
                unstash 'satfon-simulation'
                unstash 'outpost-core'
                
                dir('paffs') {
                    sh 'make build-embedded-om2'
                }
            }
        }
    }
}
