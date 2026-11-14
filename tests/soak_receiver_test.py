"""长稳观察器不能把重启、冻结、丢弃或存储超额判为通过。"""
import importlib.util
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

spec = importlib.util.spec_from_file_location('soak_receiver', Path(__file__).parents[1]/'tools/soak_receiver.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SoakValidation(unittest.TestCase):
    def setUp(self):
        self.initial = dict(identity=[1, 'start', 'cli', 'gui'], state='listening', status_age_seconds=1,
                            status_updated_utc='old',
                            source_pairs=100, analysis_lag_seconds=0, recording_lag_seconds=0,
                            disk_refused=0, event_data_bytes=50, quota_bytes=100, max_event_bytes=20,
                            disk_free_bytes=1024**3, **{key: 0 for key in module.COUNTERS})
        self.current = dict(self.initial, source_pairs=200, status_updated_utc='new')

    def test_healthy(self):
        self.assertEqual(module.faults(self.current, self.initial, self.initial), [])
        self.assertEqual(module.faults(self.initial, self.initial, self.initial), [])
        self.assertEqual(module.faults(dict(self.current, analysis_queue_capacity_seconds=4.8,
                                           analysis_lag_seconds=3), self.initial, self.initial), [])
        self.assertTrue(module.faults(dict(self.current, analysis_queue_capacity_seconds=4.8,
                                          analysis_lag_seconds=5.2), self.initial, self.initial))
        self.assertEqual(module.faults(dict(self.current, analysis_queue_capacity_seconds=4.8,
                                           analysis_inflight_seconds=.6, analysis_lag_seconds=5.2), self.initial, self.initial), [])

    def test_faults(self):
        for change in (
            {'identity': [1, 'new-start', 'cli', 'gui']}, {'state': 'stopped'},
            {'status_age_seconds': 31}, {'source_pairs': 100}, {'analysis_lag_seconds': 3},
            {'recording_lag_seconds': 3}, {'disk_refused': 1}, {'event_data_bytes': 121},
            {'disk_free_bytes': 1}, *({key: 1} for key in module.COUNTERS),
        ):
            with self.subTest(change=change):
                self.assertTrue(module.faults(dict(self.current, **change), self.initial, self.initial))

    def test_verify_history(self):
        with TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory/'owner.json').write_text(json.dumps({'owner': 'signalscope-readonly-soak'}))
            start = datetime(2026,11,6,tzinfo=timezone.utc)
            samples = []
            for index in range(3):
                stamp = (start+timedelta(seconds=60*index)).isoformat()
                samples.append(dict(self.initial, observed_utc=stamp, status_updated_utc=stamp,
                                    source_pairs=100+index*100))
            summary = dict(state='complete', requested_seconds=120, elapsed_seconds=120, errors=[],
                           initial=samples[0], latest=samples[-1], samples=3,
                           receiver_uptime_identity=self.initial['identity'])
            (directory/'summary.json').write_text(json.dumps(summary))
            def write_log(rows):
                (directory/'samples-0.jsonl').write_text('\n'.join(json.dumps(row) for row in rows))
            entries = [dict(row,sample_index=index) for index,row in enumerate(samples)]
            write_log(entries)
            self.assertEqual(module.verify_history(directory,120)['state'], 'verified')
            self.assertEqual(module.verify_history(directory,86400)['state'], 'invalid')
            write_log([entries[0], dict(entries[1], analysis_dropped_pairs=1), entries[2]])
            self.assertEqual(module.verify_history(directory,120)['state'], 'invalid')
            write_log([entries[0], entries[2]])
            self.assertEqual(module.verify_history(directory,120)['state'], 'invalid')
            (directory/'summary.json').write_text(json.dumps(dict(summary,state='running')))
            self.assertEqual(module.verify_history(directory,120)['state'], 'incomplete')


if __name__ == '__main__':
    unittest.main()
