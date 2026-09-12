import contextlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import AsyncMock, patch

from codex_passport.cli import main


class SetupTests(unittest.TestCase):
    def test_setup_displays_own_pairing_details_and_starts_selected_private_host(self):
        with tempfile.TemporaryDirectory() as directory:
            output = io.StringIO()
            service = AsyncMock()
            argv = ['codex-passport', '--state-dir', directory, 'setup', '--host', '192.168.50.2']
            with patch('sys.argv', argv), patch('codex_passport.relay.serve', service), contextlib.redirect_stdout(output):
                main()
            token = (Path(directory)/'relay-token').read_text().strip()
            self.assertIn('http://192.168.50.2:18765', output.getvalue())
            self.assertIn(token, output.getvalue())
            self.assertEqual((Path(directory)/'relay-token').stat().st_mode & 0o777, 0o600)
            service.assert_awaited_once()
            self.assertEqual(service.call_args.args[0].host, '192.168.50.2')


if __name__ == '__main__':
    unittest.main()
