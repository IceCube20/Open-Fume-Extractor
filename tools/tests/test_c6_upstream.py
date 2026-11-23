from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class C6UpstreamTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('node'), 'Node.js required')
    def test_watcher_and_notification_regressions(self):
        subprocess.run([shutil.which('node'), str(ROOT / 'tools/tests/test_c6_upstream.cjs')],
                       cwd=ROOT, check=True, timeout=30)

    def test_daily_manual_and_push_checks_do_not_grant_catalog_write(self):
        workflow = (ROOT / '.github/workflows/c6-upstream.yml').read_text()
        self.assertIn("cron: '23 6 * * *'", workflow)
        self.assertIn('workflow_dispatch:', workflow)
        self.assertIn('issues: write', workflow)
        self.assertIn('contents: read', workflow)
        self.assertNotIn('contents: write', workflow)
        self.assertNotIn('pull_request', workflow)
        self.assertIn('github.event.repository.default_branch', workflow)
        self.assertIn('persist-credentials: false', workflow)
        self.assertIn('cancel-in-progress: false', workflow)

    def test_watcher_reads_catalog_but_never_rewrites_it(self):
        source = (ROOT / 'tools/maintenance/check_c6_upstream.cjs').read_text()
        self.assertIn('fs.readFileSync(', source)
        self.assertNotIn('writeFile', source)
        self.assertNotIn('exec(', source)
        self.assertNotIn('spawn(', source)
        self.assertIn("notify ? await publish", source)
        self.assertIn("process.env.GITHUB_ACTIONS !== 'true'", source)
        self.assertIn("i.user?.login === 'github-actions[bot]'", source)


if __name__ == '__main__':
    unittest.main()
