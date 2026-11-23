from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PagesWorkflowTests(unittest.TestCase):
    def test_upload_and_deploy_use_same_attempt_specific_artifact(self):
        workflow = ROOT / '.github/workflows/pages.yml'
        if not workflow.exists():
            workflow = ROOT / 'GitHub-Release/.github/workflows/pages.yml'
        source = workflow.read_text()
        upload = re.search(r'with:\s*\n\s+name: (.+)\n\s+path:', source)
        deploy = re.search(r'artifact_name: (.+)', source)
        self.assertIsNotNone(upload)
        self.assertIsNotNone(deploy)
        self.assertEqual(upload.group(1), deploy.group(1))
        self.assertIn('${{ github.run_id }}', upload.group(1))
        self.assertIn('${{ github.run_attempt }}', upload.group(1))


if __name__ == '__main__':
    unittest.main()
