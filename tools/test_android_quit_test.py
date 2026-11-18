import unittest
from android_quit_test import exit_record


class ExitRecordTests(unittest.TestCase):
    def test_fresh_pid_not_stale_crash(self):
        text = '''ApplicationExitInfo #0:
          pid=91 reason=5 (CRASH_NATIVE) status=11
        ApplicationExitInfo #1:
          pid=92 reason=1 (EXIT_SELF) status=0'''
        self.assertEqual(exit_record(text, 92), {'reason': 1, 'status': 0})
        self.assertEqual(exit_record(text, 91), {'reason': 5, 'status': 11})

    def test_missing_or_incomplete_record_is_not_success(self):
        self.assertIsNone(exit_record('ApplicationExitInfo #0: pid=12 reason=1', 12))
        self.assertIsNone(exit_record('ApplicationExitInfo #0: pid=12 reason=1 status=0', 13))

    def test_force_stop_is_distinct(self):
        self.assertEqual(exit_record('ApplicationExitInfo #0: pid=7 reason=10 (USER_REQUESTED) status=9', 7),
                         {'reason': 10, 'status': 9})


if __name__ == '__main__': unittest.main()
