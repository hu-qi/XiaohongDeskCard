"""Prevent accidental DevEco debug key/password disclosure in shared Git."""
import unittest

from tools.check_signing_safety import (
    REPO_ROOT, RELATIVE, is_safe_shared_config
)


class HarmonySigningSafetyTests(unittest.TestCase):
    def test_committed_build_profile_contains_no_credentials(self):
        source = (REPO_ROOT / RELATIVE).read_text(encoding="utf-8")
        self.assertTrue(
            is_safe_shared_config(source),
            "Do not commit local DevEco certificate paths or signing secrets",
        )

    def test_unconfigured_or_whitespace_only_signing_is_safe(self):
        self.assertTrue(is_safe_shared_config('{"app":{"signingConfigs":[]}}'))
        self.assertTrue(
            is_safe_shared_config('{"app":{"signingConfigs":[\n  \t]}}')
        )

    def test_local_debug_signing_material_is_rejected(self):
        synthetic = (
            '{"app":{"signingConfigs":[{"name":"default",'
            '"material":{"certpath":"/Users/example/.ohos/config/debug.cer",'
            '"keyPassword":"dummy_not_a_secret",'
            '"storeFile":"/Users/example/.ohos/config/debug.p12"}}],'
            '"products":[{"signingConfig":"default"}]}}'
        )
        self.assertFalse(is_safe_shared_config(synthetic))
        self.assertFalse(is_safe_shared_config(
            '{"app":{"signingConfigs":[ { "name":"default" } ]}}'
        ))

    def test_each_signing_field_is_blocked(self):
        for field in (
            "certpath", "keyAlias", "keyPassword", "profile",
            "signAlg", "storeFile", "storePassword"
        ):
            with self.subTest(field=field):
                self.assertFalse(
                    is_safe_shared_config('{"' + field + '":"placeholder"}')
                )

    def test_personal_paths_are_blocked_without_echo(self):
        self.assertFalse(is_safe_shared_config(
            '{"configPath":"/Users/example/.ohos/config/example.p12"}'
        ))
        self.assertFalse(is_safe_shared_config(
            '{"configPath":"C:\\\\Users\\\\example\\\\example.p12"}'
        ))


if __name__ == "__main__":
    unittest.main()
