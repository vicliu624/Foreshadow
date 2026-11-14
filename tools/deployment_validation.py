"""部署前核对同一二进制的实机运行报告；不替代射频与 GUI 验证。"""


def validate_runtime_report(report, binary_sha256, algorithm):
    errors = []
    if report.get('validation_binary_sha256') != binary_sha256:
        errors.append('验证报告不是待安装的二进制')
    if report.get('detector', {}).get('algorithm') != algorithm:
        errors.append('算法标记不匹配')
    if report.get('validation_returncode') != 0 or report.get('state') != 'stopped':
        errors.append('验证进程未正常结束')
    if report.get('elapsed_seconds', 0) < 300:
        errors.append('实机运行不足 300 秒')
    for key in ('analysis_dropped_pairs', 'analysis_gaps', 'result_dropped_groups', 'result_dropped_events'):
        if report.get(key) != 0:
            errors.append(key+' 非零或缺失')
    source = report.get('source_result', {})
    if (source.get('backend') != 'librtlsdr' or source.get('status') != 'complete'
            or source.get('rejected_pairs') != 0 or source.get('driver_result') != 0):
        errors.append('硬件采集报告不合格')
    pairs = report.get('source_pairs', 0)
    if not pairs or any(report.get(key) != pairs for key in ('processed_pairs', 'recording_processed_pairs')):
        errors.append('采集、分析和录制处理计数不一致')
    if errors:
        raise ValueError('；'.join(errors))
