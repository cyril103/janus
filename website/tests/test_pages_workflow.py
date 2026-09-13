"""Guard the Pages permission boundary in the existing site CI test suite."""

from pathlib import Path
import unittest

import yaml


WORKFLOW = Path(__file__).resolve().parents[2] / ".github/workflows/pages.yml"


class PagesWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # BaseLoader preserves GitHub's `on` key instead of treating it as a bool.
        cls.workflow = yaml.load(WORKFLOW.read_text(encoding="utf-8"), Loader=yaml.BaseLoader)

    def test_only_deploy_has_publication_permissions(self):
        permissions = self.workflow["permissions"]
        self.assertEqual(permissions, {"contents": "read"})
        jobs = self.workflow["jobs"]
        self.assertIn("build", jobs)
        self.assertIn("deploy", jobs)
        for name, job in jobs.items():
            with self.subTest(job=name):
                expected = (
                    {"pages": "write", "id-token": "write"}
                    if name == "deploy" else {"contents": "read"}
                )
                self.assertEqual(job.get("permissions", permissions), expected)

    def test_pull_requests_build_and_only_main_pushes_deploy(self):
        events = self.workflow["on"]
        self.assertIn("pull_request", events)
        self.assertEqual(events["push"]["branches"], ["main"])
        jobs = self.workflow["jobs"]
        self.assertNotIn("if", jobs["build"])
        self.assertEqual(jobs["deploy"]["if"], "github.event_name == 'push'")
        self.assertEqual(jobs["deploy"]["needs"], "build")
        self.assertEqual(jobs["deploy"]["environment"]["name"], "github-pages")

    def test_pages_actions_stay_in_their_jobs(self):
        locations = {}
        for name, job in self.workflow["jobs"].items():
            for index, step in enumerate(job.get("steps", [])):
                action = step.get("uses", "").split("@", 1)[0]
                locations.setdefault(action, []).append((name, index, step))
        expected = {
            "actions/configure-pages": "deploy",
            "actions/upload-pages-artifact": "build",
            "actions/deploy-pages": "deploy",
        }
        for action, job in expected.items():
            with self.subTest(action=action):
                self.assertEqual(len(locations.get(action, [])), 1)
                self.assertEqual(locations[action][0][0], job)
        configure = locations["actions/configure-pages"][0]
        deploy = locations["actions/deploy-pages"][0]
        self.assertLess(configure[1], deploy[1])
        self.assertNotIn("if", configure[2])
        self.assertNotIn("if", deploy[2])
        upload = locations["actions/upload-pages-artifact"][0][2]
        self.assertEqual(upload["if"], "github.event_name == 'push'")
        self.assertEqual(upload["with"]["path"], "_site")


if __name__ == "__main__":
    unittest.main()
