# Security Policy

EqualizerAPO-XT is a personal project maintained by
[@115dkk](https://github.com/115dkk), who is responsible for its security and
handles every vulnerability report for it.

## Scope

- The Equalizer APO DLL. Windows loads it into the audio engine
  (`audiodg.exe`), so it processes the audio of every program on the device
  it is installed on.
- The engine host (`EqualizerAPOHost`), the ASIO driver, the VST2 and VST3
  hosting code, and the Voicemeeter client.
- The Configuration Editor, the Device Selector, and the configuration file
  parser.
- The installer, the setup tools, the Velopack updater and the update checker,
  and the workflows in this repository that build the release files.

Flaws that XT inherited from Equalizer APO 1.4.2 and still carries are in
scope. Third-party VST plugins and audio drivers are not; report those to
their authors.

## Supported versions

Only the newest release on the
[Releases page](https://github.com/115dkk/EqualizerAPO-XT/releases/latest)
gets security fixes. Installed copies update themselves, so update first and
check whether the problem is still there.

## Reporting a vulnerability

Report it privately through
[GitHub's private vulnerability reporting](https://github.com/115dkk/EqualizerAPO-XT/security/advisories/new).
Do not open a public issue, discussion, or pull request for a suspected
vulnerability.

A useful report names the XT version and build variant (for example
`x64-avx2`), the Windows version, the affected component, and the steps or the
configuration file that reproduce the problem.

## What happens next

This project is maintained in spare time, so there is no guaranteed response
time. The maintainer reads every report and answers in the report's private
thread. A confirmed vulnerability is fixed in a new release, and the
maintainer then publishes a GitHub security advisory that credits the reporter
unless the reporter asks not to be named.

There is no bug bounty.
