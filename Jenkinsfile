// Jenkinsfile (v08 - *.ast.json 생성 반영)
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

          cat > tools/ast_ci/ast_build_and_diff.py << 'PY'
#!/usr/bin/env python3
import argparse, json, subprocess, sys, hashlib
from pathlib import Path
from typing import Any, Dict, List, Set

def run(cmd: List[str], check: bool = True) -> str:
    p = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and p.returncode != 0:
        sys.stderr.write(p.stderr)
        raise SystemExit(p.returncode)
    return p.stdout

def run_shell(cmd: str) -> str:
    p = subprocess.run(["bash","-lc",cmd], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return (p.stdout or "") + (p.stderr or "")

def sha1_text(s: str) -> str:
    return hashlib.sha1(s.encode("utf-8", errors="ignore")).hexdigest()

def git_rev(ref: str) -> str:
    return run(["git","rev-parse",ref]).strip()

def ensure_parent(p: Path) -> None:
    p.parent.mkdir(parents=True, exist_ok=True)

def list_changed_files(base: str, head: str) -> List[str]:
    out = run(["git","diff","--name-only",f"{base}..{head}"])
    return [x for x in out.splitlines() if x]

def build_compile_db(build_cmd: str) -> bool:
    _ = run_shell(f"bear -- {build_cmd}")
    return Path("compile_commands.json").exists()

def read_compile_db() -> Dict[str, List[str]]:
    db = Path("compile_commands.json")
    if not db.exists():
        return {}
    data = json.loads(db.read_text(encoding="utf-8", errors="ignore"))
    mapping: Dict[str, List[str]] = {}
    for e in data:
        fp = e.get("file")
        if not fp:
            continue
        absf = str(Path(fp).resolve())
        args = e.get("arguments") or e.get("command","").split()
        if args and (args[0].endswith("clang") or args[0].endswith("gcc") or args[0].endswith("cc")):
            args = args[1:]
        mapping[absf] = args
    return mapping

def filter_args(args: List[str]) -> List[str]:
    skip = {"-c","-MMD","-MP"}
    out: List[str] = []
    i=0
    while i < len(args):
        a=args[i]
        if a in skip:
            i+=1
        elif a in ("-o","-MF","-MT","-MQ"):
            i+=2
        elif a.endswith(".c"):
            i+=1
        else:
            out.append(a); i+=1
    return out

def clang_ast(clang: str, src: str, flags: List[str]) -> Dict[str, Any]:
    cmd = [clang,"-Xclang","-ast-dump=json","-fsyntax-only",src] + flags
    return json.loads(run(cmd))

def normalise(node: Any) -> str:
    if not isinstance(node, dict):
        return ""
    s = f"{node.get('kind','')}|{node.get('name','')}"
    for c in node.get("inner", []) or []:
        s += normalise(c)
    return s

def index_functions(ast: Dict[str, Any]) -> Dict[str, Dict[str, Any]]:
    out: Dict[str, Dict[str, Any]] = {}
    def walk(n: Any):
        if isinstance(n, dict):
            if n.get("kind")=="FunctionDecl" and n.get("name"):
                out[n["name"]] = n
            for c in n.get("inner", []) or []:
                walk(c)
    walk(ast)
    return out

def diff_functions(a: Dict[str, Any], b: Dict[str, Any]) -> Dict[str, List[str]]:
    fa = index_functions(a)
    fb = index_functions(b)
    return {
        "only_before": sorted(set(fa) - set(fb)),
        "only_after":  sorted(set(fb) - set(fa)),
        "changed": sorted(
            f for f in (fa.keys() & fb.keys())
            if sha1_text(normalise(fa[f])) != sha1_text(normalise(fb[f]))
        )
    }

def list_all_rt_tus(root: Path) -> List[Path]:
    return sorted((root/"os/rt/src").rglob("*.c"))

def select_incremental_tus(changed: List[str], root: Path) -> List[Path]:
    tus: Set[Path] = set()
    header_changed = any(p.startswith("os/rt/include/") and p.endswith(".h") for p in changed)
    for p in changed:
        if p.startswith("os/rt/src/") and p.endswith(".c"):
            tus.add(root/p)
    if header_changed:
        tus |= set(list_all_rt_tus(root))
    return sorted(tus)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--base", required=True)
    ap.add_argument("--head", required=True)
    ap.add_argument("--mode", choices=["baseline","incremental"], required=True)
    ap.add_argument("--clang", default="clang")
    ap.add_argument("--build-cmd", default="")
    ap.add_argument("--fallback-includes", nargs="*", default=["-Ios/rt/include"])
    args = ap.parse_args()

    root = Path(".").resolve()
    out = Path(args.outdir); out.mkdir(parents=True, exist_ok=True)

    base_commit = git_rev(args.base)
    head_commit = git_rev(args.head)

    compile_db: Dict[str, List[str]] = {}
    if args.build_cmd and build_compile_db(args.build_cmd):
        compile_db = read_compile_db()

    if args.mode == "baseline":
        tus = list_all_rt_tus(root)
        changed_files = ["(baseline 초기 생성)"]
    else:
        changed_files = list_changed_files(args.base, args.head)
        tus = select_incremental_tus(changed_files, root)

    results = []
    for tu in tus:
        rel = tu.relative_to(root)

        before = out / f"{rel}.before.c"
        after  = out / f"{rel}.after.c"
        diffp  = out / f"{rel}.diff.json"

        # ✅ 추가: AST 덤프 파일 생성 (사용자가 찾는 *.ast.json)
        ast_before_p = out / f"{rel}.before.ast.json"
        ast_after_p  = out / f"{rel}.after.ast.json"

        ensure_parent(before); ensure_parent(after); ensure_parent(diffp)
        ensure_parent(ast_before_p); ensure_parent(ast_after_p)

        before.write_text(run(["git","show",f"{base_commit}:{rel}"], check=False), encoding="utf-8", errors="ignore")
        after.write_text(run(["git","show",f"{head_commit}:{rel}"], check=False), encoding="utf-8", errors="ignore")

        flags = compile_db.get(str(tu.resolve()), args.fallback_includes)
        flags = filter_args(flags)

        ast_b = clang_ast(args.clang, str(before), flags)
        ast_a = clang_ast(args.clang, str(after),  flags)

        # ✅ 추가: AST JSON 저장
        ast_before_p.write_text(json.dumps(ast_b, indent=2), encoding="utf-8")
        ast_after_p.write_text(json.dumps(ast_a, indent=2), encoding="utf-8")

        diff = diff_functions(ast_b, ast_a)
        diffp.write_text(json.dumps(diff, indent=2), encoding="utf-8")

        results.append({"tu": str(rel), **diff})

    summary = {
        "mode": args.mode,
        "base_commit": base_commit,
        "head_commit": head_commit,
        "changed_files": changed_files,
        "results": results
    }
    (out/"summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

if __name__ == "__main__":
    main()
PY

          chmod +x tools/ast_ci/ast_build_and_diff.py
          ls -la tools/ast_ci
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
