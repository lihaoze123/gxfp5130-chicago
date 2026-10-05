#!/usr/bin/env python3
"""Interactive real fprintd enrollment/verification; private statistics only."""
import argparse
import datetime
import json
import os
from pathlib import Path
import pwd
import re
import shutil
import subprocess
import tempfile
import threading
import time




def classify(output):
    statuses = re.findall(r'Verify result:\s*([\w-]+)\s*\(done\)', output)
    return statuses[-1] if statuses else 'error-or-timeout'


def extract_scores(journal):
    return [int(x) for x in re.findall(r'Chicago verify .*?score=(-?\d+)', journal)]


def enrollment_guidance(line):
    match = re.search(r'Chicago enrollment accepted=(\d+)/12 records=(\d+) position-retry=(\d+)', line)
    if not match:
        return None
    stage, _, direction = map(int, match.groups())
    moves = {1: '向下', 2: '向上', 3: '向右', 4: '向左'}
    if direction in moves:
        return (f'位置覆盖提示：下一次请{moves[direction]}小幅移动手指，保持部分区域重叠；'
                '抬手后等就绪再触摸，不必回到固定角度。')
    if stage == 4:
        return '后续采集请小幅改变落点、逐渐覆盖中间和两侧；每次保留重叠区域，避免大幅旋转。'
    return None


def settled_journal(cursor, action):
    # The CLI can finish before journalctl -f delivers the final score. Read
    # from the original cursor again instead of killing the reader too early.
    command = ['journalctl', '-u', 'fprintd.service', '-o', 'cat', '--no-pager']
    command += ['--after-cursor', cursor] if cursor else ['-n', '200']
    deadline = time.monotonic() + 2
    while True:
        journal = subprocess.check_output(command, text=True, stderr=subprocess.STDOUT)
        if action != 'verify' or extract_scores(journal) or time.monotonic() >= deadline:
            return journal
        time.sleep(0.1)


def run_client(action, user, folder, label, limit, finger):
    """Journal readiness comes from the device, not the early CLI start banner."""
    cursor_text = subprocess.check_output(
        ['journalctl', '-u', 'fprintd.service', '-n', '1', '--show-cursor', '--no-pager'],
        text=True)
    cursor = re.search(r'^-- cursor: (.+)$', cursor_text, re.M)
    journal_cmd = ['journalctl', '-u', 'fprintd.service', '-f', '-o', 'cat', '--no-pager']
    journal_cmd += ['--after-cursor', cursor[1]] if cursor else ['-n', '0']
    watcher = subprocess.Popen(journal_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, bufsize=1)
    lines = []

    def watch():
        with (folder / f'{label}.journal.log').open('w') as log:
            for line in watcher.stdout:
                # No raw images, feature descriptors, keys or template payloads.
                if any(x in line for x in ('Chicago ', 'Finger needed ', 'Saved print updated',
                                          'print saved', 'assertion', 'CRITICAL')):
                    log.write(line)
                    log.flush()
                    lines.append(line)
                if 'Finger needed 1' in line:
                    print('\n>>> 传感器已就绪：现在轻放手指，保持到结果出现，再完全抬起。', flush=True)
                if action == 'enroll':
                    guidance = enrollment_guidance(line)
                    if guidance:
                        print(f'\n>>> {guidance}', flush=True)

    reader = threading.Thread(target=watch, daemon=True)
    reader.start()
    begin = time.monotonic()
    output = []
    code = None
    settled = None
    try:
        with (folder / f'{label}.cli.log').open('w') as log:
            client = subprocess.Popen(
                ['timeout', '--signal=INT', '--kill-after=5', str(limit),
                 f'fprintd-{action}', '-f', finger, user],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1,
                env={**os.environ, 'LC_ALL': 'C'})
            try:
                for line in client.stdout:
                    print(line, end='', flush=True)
                    log.write(line)
                    log.flush()
                    output.append(line)
                code = client.wait()
                settled = settled_journal(cursor[1] if cursor else None, action)
            finally:
                if client.poll() is None:
                    client.terminate()
                    try:
                        client.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        client.kill()
                        client.wait()
    finally:
        watcher.terminate()
        reader.join(timeout=3)
        watcher.wait(timeout=5)
    if settled is not None:
        lines = [line + '\n' for line in settled.splitlines() if any(
            x in line for x in ('Chicago ', 'Finger needed ', 'Saved print updated',
                               'print saved', 'assertion', 'CRITICAL'))]
        (folder / f'{label}.journal.log').write_text(''.join(lines))
    text = ''.join(output)
    return {
        'label': label, 'result': classify(text) if action == 'verify' else
        ('enroll-completed' if 'Enroll result: enroll-completed' in text else 'enroll-failed'),
        'exit_code': code, 'seconds': round(time.monotonic() - begin, 2),
        'scores': extract_scores(''.join(lines)),
        'journal_cursor': cursor[1] if cursor else None,
    }



def import_files(args):
    import hashlib
    import extract_config
    if os.geteuid() != 0:
        raise SystemExit('导入需要 root 权限，请使用 sudo。')
    psk = Path(args.psk).read_bytes()
    calib = Path(args.calibration).read_bytes()
    if len(psk) != 32:
        raise SystemExit('PSK 必须为 32 字节；未写入任何文件。')
    if len(calib) != 16 + 0x224b0 or calib[16+0x22490:16+0x22490+len(b'Preprocess_v_1.01.01\0')] != b'Preprocess_v_1.01.01\0':
        raise SystemExit('校准文件长度或版本不符；未写入任何文件。')
    source = Path(args.config)
    if source.is_file() and source.stat().st_size == 224:
        cfg = source.read_bytes()
    else:
        data = Path(extract_config.find_dll(str(source))).read_bytes()
        candidates = [b for _, b in extract_config.candidates(data) if b[0] == 0x70]
        if len(candidates) != 1:
            raise SystemExit('无法唯一定位 ChicagoHS 配置；未写入任何文件。')
        cfg = candidates[0]
    if hashlib.sha256(cfg).hexdigest() != extract_config.KNOWN_SHA256:
        raise SystemExit('配置不符合已测试模板；未写入任何文件。')
    target = Path('/var/lib/fprintd/gxfp')
    target.mkdir(mode=0o700, parents=True, exist_ok=True)
    blobs = {'psk_raw32.bin': psk, 'goodix_calib.dat': calib, 'chicagohs_cfg.bin': cfg}
    # Refuse replacement of already-installed private data; rerunning is idempotent.
    for name, blob in blobs.items():
        f = target/name
        if f.is_symlink() or (f.exists() and f.read_bytes() != blob):
            raise SystemExit(f'{f} 已存在且内容不同，请先人工核实；未写入任何文件。')
    if subprocess.run(['systemctl', 'is-active', '--quiet', 'fprintd.service']).returncode == 0:
        raise SystemExit('请先 systemctl stop fprintd，避免采集期间替换校准。')
    os.chmod(target, 0o700)
    os.chown(target, 0, 0)
    for name, blob in blobs.items():
        fd, temp = tempfile.mkstemp(prefix='.import-', dir=target)
        try:
            with os.fdopen(fd, 'wb') as stream:
                stream.write(blob)
            os.chown(temp, 0, 0)
            os.replace(temp, target/name)
        finally:
            Path(temp).unlink(missing_ok=True)
    print('已导入 PSK、配置和校准；目录 0700，文件 0600。')


def main():
    parser = argparse.ArgumentParser(description='ChicagoHS 私有文件导入、录入引导和持续验证')
    sub = parser.add_subparsers(dest='action', required=True)
    imp = sub.add_parser('import')
    imp.add_argument('--psk', required=True)
    imp.add_argument('--calibration', required=True)
    imp.add_argument('--config', required=True, help='224 字节配置、gfspi.dll 或 Windows 挂载目录')
    for mode in ('enroll', 'verify'):
        cmd = sub.add_parser(mode)
        cmd.add_argument('--user', default=os.environ.get('SUDO_USER') or pwd.getpwuid(os.getuid()).pw_name)
        cmd.add_argument('--finger', default='right-index-finger')
    args = parser.parse_args()
    os.umask(0o077)
    if args.action == 'import':
        import_files(args)
        return 0
    account = pwd.getpwnam(args.user)
    if args.user == 'root':
        parser.error('请用 --user 指定需要识别的普通用户。')
    if os.geteuid() != 0:
        parser.error('引导和分数读取需要 root 日志权限，请用 sudo（标准 fprintd 命令可按系统授权运行）。')
    for command in ('fprintd-'+args.action, 'journalctl', 'timeout'):
        if shutil.which(command) is None:
            parser.error(f'找不到 {command}，请先启用 NixOS 模块。')
    base = Path(account.pw_dir)/'.local/state/gxfp5130-chicago'
    for directory in (Path(account.pw_dir)/'.local', Path(account.pw_dir)/'.local/state', base):
        if not directory.exists():
            directory.mkdir(mode=0o700)
            os.chown(directory, account.pw_uid, account.pw_gid)
    folder = Path(tempfile.mkdtemp(prefix=args.action+'-', dir=base))
    records = []
    print(f'私有统计目录：{folder}', flush=True)
    print('初始化时完全移开手指；等“传感器已就绪”后触摸。', flush=True)
    try:
        if args.action == 'enroll':
            print('录入 12 个有效阶段；逐渐覆盖中心和两侧，每次保留重叠。', flush=True)
            records.append(run_client('enroll', args.user, folder, 'enroll', 660, args.finger))
        else:
            print('复用现有模板。不标记手指；输入 q 或 Ctrl+C 结束。', flush=True)
            number = 1
            while True:
                answer = input(f'[{number}] 完全抬手后按 Enter 开始（q 结束）：').strip().lower()
                if answer in ('q', 'quit', 'exit'):
                    break
                if answer:
                    continue
                record = run_client('verify', args.user, folder, f'verify-{number:04}', 100, args.finger)
                records.append(record)
                result = {'verify-match':'匹配', 'verify-no-match':'不匹配'}.get(record['result'], '错误或超时')
                scores = ', '.join(map(str, record['scores'])) or '未取得；请检查 debug 和本轮日志'
                print(f'>>> {result}；分数：{scores}', flush=True)
                (folder/'results.json').write_text(json.dumps(records, ensure_ascii=False, indent=2)+'\n')
                number += 1
    except (KeyboardInterrupt, EOFError):
        print('\n已停止。')
    finally:
        (folder/'results.json').write_text(json.dumps(records, ensure_ascii=False, indent=2)+'\n')
        for f in [base, folder, *folder.iterdir()]:
            os.chown(f, account.pw_uid, account.pw_gid)
        print(f'结果：{folder / "results.json"}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
