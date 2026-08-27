import os

lit_config.load_config(
    config,
    os.path.abspath(
        os.path.join(
            os.path.dirname(__file__), "..", "..", "runners", "diagnostics", "lit.cfg.py"
        )
    ),
)
