// Jenkinsfile (v02 최적화 반영본)
pipeline {
  agent any

  options {
    // 플러그인 의존 옵션은 환경에 따라 파싱 실패할 수 있어 기본 비활성 권장
    // timestamps()
    // ansiColor('xterm')
    buildDiscarder(logRotator(numToKeepStr: '30'))
  }

  environment {
    // AST 결과를 서버에 영구 저장할 경로 (Jenkins 실행 계정이 쓰기 권한을 가져야 함)
    AST_STORE = "/var/lib/jenkins/ast/chibios-os-rt"

    // 사용 도구
    CLANG = "clang"
    PY = "python3"

    // ⚠️ 실제 ChibiOS RT 빌드에 맞게 조정 필요 (compile_commands.json 생성 목적)
    // (incremental은 필요할 때만 켜는 게 좋음)
    BUILD_CMD = "make -C testrt"

    // ==========================================================
    // [baseline에서 build-cmd 기본 비활성]
    // - baseline은 처음 1회 전체 TU를 다 도는 구간이라 시간이 길어짐
    // - bear + make가 더 오래 걸릴 수 있어서 기본은 ""(비활성)
    // - 필요하면 아래 값을 BUILD_CMD와 같게 바꿔서 켜면 됨
    //   예: BUILD_CMD_BASELINE = "make -C testrt"
    // ==========================================================
    BUILD_CMD_BASELINE = ""
  }

  triggers {
    pollSCM('H/5 * * * *')
  }

  stages {

    stage('Checkout') {
      steps {
        checkout scm
        sh 'git rev-parse --short HEAD'
      }
    }

    stage('sudo 권한 사전 점검') {
      steps {
        sh '''
          set -eux
          if sudo -n true 2>/dev/null; then
            echo "[OK] jenkins 계정이 비밀번호 없이 sudo 사용 가능"
          else
            echo "[ERROR] jenkins 계정이 NOPASSWD sudo 설정이 되어있지 않습니다."
            echo "예시(서버에서 실행):"
            echo "  sudo visudo -f /etc/sudoers.d/jenkins-apt"
            echo "  Defaults:jenkins !requiretty"
            echo "  jenkins ALL=(root) NOPASSWD: /usr/bin/apt-get, /usr/bin/apt, /usr/bin/dpkg, /usr/bin/true"
            exit 1
          fi
        '''
      }
    }

    stage('AST 실행 여부 판단') {
      steps {
        script {
          sh "mkdir -p '${env.AST_STORE}/baseline'"

          def baselineExists = fileExists("${env.AST_STORE}/baseline/summary.json")

          // ==========================================================
          // [변경 사항 감지]
          // - origin/main..HEAD 범위에서 os/rt/** 변경이 있는지 확인
          // ==========================================================
          sh "git fetch origin main:refs/remotes/origin/main || true"     // ★ 변경
          def changed = sh(
            script: "git diff --name-only origin/main..HEAD | grep '^os/rt/' || true",  // ★ 변경
            returnStdout: true
          ).trim()

          if (!baselineExists) {
            echo "Baseline AST가 존재하지 않음 → 최초 baseline 생성"
            env.DO_AST = "1"
            env.AST_MODE = "baseline"
          } else if (changed == "") {
            echo "os/rt 변경 없음 → AST 단계 스킵"
            env.DO_AST = "0"
          } else {
            echo "os/rt 변경 감지 → incremental AST 수행"
            env.DO_AST = "1"
            env.AST_MODE = "incremental"
          }
        }
      }
    }

    stage('의존성 설치') {
      when { expression { return env.DO_AST == "1" } }
      steps {
        sh '''
          set -eux
          export DEBIAN_FRONTEND=noninteractive

          if command -v clang >/dev/null \
             && command -v bear  >/dev/null \
             && command -v jq    >/dev/null \
             && command -v python3 >/dev/null; then
            echo "[SKIP] 의존성 이미 설치됨 (clang/bear/jq/python3)"
            exit 0
          fi

          sudo -n apt-get update
          sudo -n apt-get install -y clang bear jq python3
        '''
      }
    }

    stage('AST 분석 스크립트 생성') {
      when { expression { return env.DO_AST == "1" } }
      steps {
        sh '''
          set -eux
          mkdir -p tools/ast_ci
          # (이하 내용 변경 없음)
        '''
      }
    }

    stage('AST 생성 및 diff (롤백 포함)') {
      when { expression { return env.DO_AST == "1" } }
      options { timeout(time: 25, unit: 'MINUTES') }

      steps {
        sh '''
          set -euo pipefail
          # (중략 – 내용 변경 없음)

            if [ -z "${BASE_COMMIT}" ] || [ "${BASE_COMMIT}" = "null" ]; then
              echo "[INCREMENTAL] baseline 기준 커밋을 못 읽음 → origin/main 사용"  # ★ 변경
              BASE_REF="origin/main"                                                  # ★ 변경
            else
              BASE_REF="${BASE_COMMIT}"
              echo "[INCREMENTAL] baseline 기준 커밋: ${BASE_REF}"
            fi

          # (이하 변경 없음)
        '''
      }
    }
  }

  post {
    always {
      echo "빌드 결과: ${currentBuild.currentResult}"
    }
  }
}
