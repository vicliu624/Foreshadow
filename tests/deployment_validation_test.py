"""验证不合格报告不会被部署门禁接受。"""
import copy
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).parents[1]/'tools'))
from deployment_validation import validate_runtime_report


class DeploymentReport(unittest.TestCase):
    def setUp(self):
        self.report = dict(validation_binary_sha256='sha', detector={'algorithm': 'v3.1'},
                           validation_returncode=0, state='stopped', elapsed_seconds=300,
                           analysis_dropped_pairs=0, analysis_gaps=0, result_dropped_groups=0,
                           result_dropped_events=0, source_pairs=525000000, processed_pairs=525000000,
                           recording_processed_pairs=525000000,
                           source_result=dict(backend='librtlsdr', status='complete', rejected_pairs=0, driver_result=0))

    def test_valid(self):
        validate_runtime_report(self.report, 'sha', 'v3.1')

    def test_reject(self):
        for change in ({'validation_binary_sha256': 'other'}, {'state': 'listening'},
                       {'elapsed_seconds': 299}, {'analysis_dropped_pairs': 1}, {'analysis_gaps': 1},
                       {'result_dropped_events': 1}, {'validation_returncode': 1},
                       {'source_pairs': 0}, {'processed_pairs': 1}, {'recording_processed_pairs': 1},
                       {'source_result': {'backend': 'synthetic'}}, {'detector': {'algorithm': 'v4'}}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_runtime_report(dict(copy.deepcopy(self.report), **change), 'sha', 'v3.1')


if __name__ == '__main__':
    unittest.main()
