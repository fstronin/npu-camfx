The Ayatana AppIndicator typelib is provided by the Ubuntu package
`gir1.2-ayatanaappindicator3-0.1` (tested with 0.5.94-1build1). The runtime
library `libayatana-appindicator3-1` is separate and does not include the
typelib.

A local development checkout may keep a copy of
`AyatanaAppIndicator3-0.1.typelib` here. The public repository does not
track that binary; install the package above instead. `scripts/install.sh`
copies the local file when it exists and otherwise falls back to the
system-wide typelib.
