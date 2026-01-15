// Jenkinsfile (v06)
pipeline {
  agent any

  options {
    buildDiscarder(logRotator(numToKeepStr: '30'))
  }

  environment {
    AST_STORE = "/var/lib/jenkins/ast/chibios-os-rt"
    CLANG = "clang"
    PY = "python3"
    BUILD_CMD = "make -C testrt"
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

          sh "git fetch origin main:refs/remotes/origin/main || true"
          def changed = sh(
            script: "git diff --name-only origin/main..HEAD | grep '^os/rt/' || true",
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
          bash -lc '
          set -euo pipefail

          COMMIT=$(git rev-parse --short HEAD)
          AST_ROOT="${AST_STORE}"
          BASELINE_DIR="${AST_ROOT}/baseline"

          mkdir -p "${AST_ROOT}"
          mkdir -p "ast_out"

          TS=$(date +%Y%m%d_%H%M%S)
          TMP_BASE="${AST_ROOT}/.tmp_baseline_${COMMIT}_${TS}"
          TMP_COMMIT="${AST_ROOT}/.tmp_commit_${COMMIT}_${TS}"

          rollback() {
            echo "[ROLLBACK] 빌드 실패 감지 → 임시 결과물 정리 및(필요 시) baseline 복구"
            rm -rf "${TMP_BASE}" "${TMP_COMMIT}" || true

            if [ -n "${BASELINE_BACKUP:-}" ] && [ -d "${BASELINE_BACKUP}" ]; then
              echo "[ROLLBACK] baseline 복구 수행: ${BASELINE_BACKUP} → ${BASELINE_DIR}"
              rm -rf "${BASELINE_DIR}" || true
              mv "${BASELINE_BACKUP}" "${BASELINE_DIR}" || true
            fi
          }
          trap rollback ERR

          if [ "${AST_MODE}" = "baseline" ]; then
            OUT="ast_out/baseline_${COMMIT}"
            mkdir -p "$OUT"

            echo "[BASELINE] 전체 TU AST 생성 시작"

            # ✅ 수정 포인트: BUILD_CMD_BASELINE이 없더라도 nounset(-u)에서 죽지 않게 기본값 처리
            BASELINE_BUILD_CMD="${BUILD_CMD_BASELINE:-}"

            ${PY} tools/ast_ci/ast_build_and_diff.py \
              --outdir "$OUT" \
              --base "HEAD" \
              --head "HEAD" \
              --mode "baseline" \
              --build-cmd "${BASELINE_BUILD_CMD}"

            mkdir -p "${TMP_BASE}"
            rsync -a --delete "$OUT/" "${TMP_BASE}/"

            if [ -d "${BASELINE_DIR}" ] && [ "$(ls -A "${BASELINE_DIR}" 2>/dev/null || true)" != "" ]; then
              BASELINE_BACKUP="${AST_ROOT}/.backup_baseline_${TS}"
              echo "[BASELINE] 기존 baseline 백업: ${BASELINE_DIR} → ${BASELINE_BACKUP}"
              mv "${BASELINE_DIR}" "${BASELINE_BACKUP}"
            fi

            echo "[BASELINE] baseline 원자적 교체: ${TMP_BASE} → ${BASELINE_DIR}"
            rm -rf "${BASELINE_DIR}" || true
            mv "${TMP_BASE}" "${BASELINE_DIR}"

            if [ -n "${BASELINE_BACKUP:-}" ] && [ -d "${BASELINE_BACKUP}" ]; then
              echo "[BASELINE] 교체 성공 → 이전 baseline 백업 정리: ${BASELINE_BACKUP}"
              rm -rf "${BASELINE_BACKUP}"
            fi

            echo "[BASELINE] 완료: ${BASELINE_DIR}/summary.json 생성 여부 확인"
            ls -la "${BASELINE_DIR}" || true

          else
            BASE_COMMIT=""
            if [ -f "${BASELINE_DIR}/summary.json" ]; then
              BASE_COMMIT=$(jq -r ".head_commit // .headCommit // empty" "${BASELINE_DIR}/summary.json" || true)
            fi
            if [ -z "${BASE_COMMIT}" ] || [ "${BASE_COMMIT}" = "null" ]; then
              echo "[INCREMENTAL] baseline 기준 커밋을 못 읽음 → origin/main 사용"
              BASE_REF="origin/main"
            else
              BASE_REF="${BASE_COMMIT}"
              echo "[INCREMENTAL] baseline 기준 커밋: ${BASE_REF}"
            fi

            OUT="ast_out/${COMMIT}"
            mkdir -p "$OUT"

            echo "[INCREMENTAL] 변경 TU AST 생성 + diff 시작"
            ${PY} tools/ast_ci/ast_build_and_diff.py \
              --outdir "$OUT" \
              --base "${BASE_REF}" \
              --head "HEAD" \
              --mode "incremental" \
              --build-cmd "${BUILD_CMD}"

            mkdir -p "${TMP_COMMIT}"
            rsync -a --delete "$OUT/" "${TMP_COMMIT}/"

            FINAL_COMMIT_DIR="${AST_ROOT}/${COMMIT}"

            if [ -d "${FINAL_COMMIT_DIR}" ] && [ "$(ls -A "${FINAL_COMMIT_DIR}" 2>/dev/null || true)" != "" ]; then
              COMMIT_BACKUP="${AST_ROOT}/.backup_commit_${COMMIT}_${TS}"
              echo "[INCREMENTAL] 기존 커밋 결과 백업: ${FINAL_COMMIT_DIR} → ${COMMIT_BACKUP}"
              mv "${FINAL_COMMIT_DIR}" "${COMMIT_BACKUP}"
            fi

            echo "[INCREMENTAL] 커밋 결과 원자적 교체: ${TMP_COMMIT} → ${FINAL_COMMIT_DIR}"
            rm -rf "${FINAL_COMMIT_DIR}" || true
            mv "${TMP_COMMIT}" "${FINAL_COMMIT_DIR}"

            if [ -n "${COMMIT_BACKUP:-}" ] && [ -d "${COMMIT_BACKUP}" ]; then
              echo "[INCREMENTAL] 교체 성공 → 이전 커밋 결과 백업 정리: ${COMMIT_BACKUP}"
              rm -rf "${COMMIT_BACKUP}"
            fi

            echo "[INCREMENTAL] 완료: ${FINAL_COMMIT_DIR}/summary.json 확인"
            ls -la "${FINAL_COMMIT_DIR}" || true
          fi

          trap - ERR
          '
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
