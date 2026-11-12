# Release trust: antivirus false positives, provenance, signing (owner steps)

What the build does by itself and what the owner does by hand for each release, so players can trust the files
(BUG-0784, GitHub #200, #201). Everything in "The owner" sections is done in the owner's name and only by the owner:
nobody else submits files, posts answers or changes GitHub settings.

## What the build does by itself

* **Version information** in `SporeCoopSetup.exe`, `steam_api.sporekit.dll`, `sporekit.dll`, `mods\coop.dll`
  (installer/version.rc: company, description, original file name, MIT copyright, version = `COOP_VERSION`); the
  setup's manifest has the same version. `tools/package.sh` fails when a release binary lacks it
  (`tools/pe_version.py --expect <version>`).
* **SHA256SUMS.txt** for the zip, the pack and the setup (the release job; the in-game updater checks the pack
  against it).
* **Build provenance attestations** (the release job, `actions/attest-build-provenance`, free for public
  repositories, no setting needed): the zip, the pack, the setup and the three DLLs inside the zip. Anyone checks a
  file with `gh attestation verify <file> --repo WoodyWizard/SporeCoop`. The step runs after the publish and may
  fail without blocking the release: check it once per release (below).
* **Nearly reproducible binaries:** two builds of one commit differ only in the link time stamp and the CodeView
  id; `tools/pe_version.py --repro-sha256 <stripped file>` gives the same value for both (checked 2026-11-12 with
  two checkouts in different folders: all four binaries equal).
* No packer, release binaries stripped, elevation only when the game folder needs it (the setup's manifest is
  `asInvoker`).

## The owner, once

1. **Private vulnerability reporting** (SECURITY.md points to it): GitHub > the repository > Settings > Security
   (Advanced Security) > *Private vulnerability reporting* > Enable.
2. Nothing to enable for attestations: public repositories have them on every plan.

## The owner, after each release

### 1. Check the attestation (2 minutes)

```
gh release download vX.Y.Z --repo WoodyWizard/SporeCoop --dir /tmp/scv && cd /tmp/scv
gh attestation verify SporeCoopSetup.exe --repo WoodyWizard/SporeCoop
unzip -q SporeCoop-X.Y.Z.zip -d z && gh attestation verify z/SporeCoop/steam_api.sporekit.dll --repo WoodyWizard/SporeCoop
```

Both must print "Verification succeeded". If the job's step "attest the build provenance" failed (the run's page
shows it with a warning), do not re-run the release job (it would stop at `gh release create`, the release exists):
the release stays without attestations (its SHA256SUMS.txt still works), and the next agent fixes the workflow for
the next release.

### 2. Submit the flagged files as false positives

Upload the release's `SporeCoopSetup.exe` and `steam_api.sporekit.dll` (the files players get) to
[VirusTotal](https://www.virustotal.com/gui/home/upload) first (as the owner, signed in or not): it shows which
engines flag them today. Submit to every vendor that flags a file; the most important ones, by players reached:

| Vendor | Where | Notes |
|---|---|---|
| Microsoft Defender | https://www.microsoft.com/en-us/wdsi/filesubmission | Sign in with a Microsoft account, choose **Software developer**, product *Microsoft Defender Antivirus*, "I believe this file should not be detected" (incorrectly detected). One file per submission (zip several files without a password, or submit each). Answers in 1-3 days; the result page shows the decision. This is the one that matters most (`Wacatac.B!ml`). |
| Bitdefender | https://www.bitdefender.com/submit/ (sample submission, choose "false positive"), or e-mail virus_submission@bitdefender.com | Also covers engines built on Bitdefender (many of the small VirusTotal names: Emsisoft, G Data, eScan, ...). |
| 360 Total Security | https://www.360totalsecurity.com/en/suspicion/false-positive/ | The form takes no attachment: send the complaint, then reply to their e-mail with the file. |
| Kaspersky | https://opentip.kaspersky.com/ (upload, then "Request reanalysis"), or newvirus@kaspersky.com | |
| Avast / AVG (Gen Digital) | https://www.avast.com/report-false-positive | |
| ESET | samples@eset.com, subject `False Positive: <file name>` | Attach the file; their current rules: https://support.eset.com (search "submit a false positive"). |
| Others | https://docs.virustotal.com/docs/false-positive-contacts | VirusTotal's list of every engine's false-positive contact. |

Check each vendor's page before sending: the addresses above were taken from public lists on 2026-11-12 and
vendors move their forms.

**Text for the forms and e-mails** (replace X.Y.Z, the detection name and the SHA-256 from SHA256SUMS.txt):

```
Subject: False positive: SporeCoopSetup.exe / steam_api.sporekit.dll (SporeCoop X.Y.Z)

Hello,

your product detects files of SporeCoop X.Y.Z, an open source (MIT) co-op mod for the game SPORE, as <detection
name>. We believe this is a false positive.

Files and SHA-256:
  SporeCoopSetup.exe       <sha256>
  steam_api.sporekit.dll   <sha256>

What they are:
* SporeCoopSetup.exe installs the mod: it downloads the release from the project's GitHub releases, checks it
  against the release's SHA256SUMS.txt and copies the mod's DLLs into the game folder.
* steam_api.sporekit.dll is the mod's loader. It is installed as the game's steam_api.dll (the original is kept as
  steam_api_orig.dll), forwards every Steam API call to the original and loads the mod's framework sporekit.dll.

The source code is public: https://github.com/WoodyWizard/SporeCoop (loader/loader.c, installer/, updater/).
The binaries are built by GitHub Actions from the release tag vX.Y.Z (no manual uploads); the build provenance can be
verified with: gh attestation verify <file> --repo WoodyWizard/SporeCoop
Release: https://github.com/WoodyWizard/SporeCoop/releases/tag/vX.Y.Z

Players report the detection in https://github.com/WoodyWizard/SporeCoop/issues/200 and /201.

Thank you,
<name>, maintainer of SporeCoop
```

Keep the answers (ticket numbers, "detection removed in definitions ...") in BUG-0784's History or a new line per
release here, so the next release knows which vendors already whitelisted earlier versions.

### 3. Answer the players (once 0.7.13 is out)

Draft answers for GitHub #200 and #201 (post only after reading them again; the owner says yes to each text):

**#201:**

```
Thank you for checking the hashes and for asking before allowing anything.

Yes, these are false positives. Antivirus heuristics (Defender's "!ml" detections are machine learning) react to
what a mod loader has to do: SporeCoop's loader takes the place of the game's steam_api.dll (the original is kept
as steam_api_orig.dll and gets every Steam call), the mod hooks game functions, opens a network connection for
co-op, and the setup downloads the release from GitHub and copies DLLs into the game folder. The files are not
code-signed yet.

How to verify a release yourself (all in docs/INSTALL.md, "Antivirus warnings and verifying a release"):
* SHA256SUMS.txt on the release page (as you did);
* since 0.7.13 every file, also the DLLs inside the zip, has a signed build provenance from GitHub Actions:
  gh attestation verify SporeCoopSetup.exe --repo WoodyWizard/SporeCoop
  (it shows the workflow run and the commit that built it; nobody uploads binaries by hand);
* since 0.7.13 the files carry version information (Properties > Details);
* the source is all here, and you can rebuild it (README, "Building from source").

Without the setup: use the release zip and install.bat (a plain batch file you can read first).
We have reported the files to Microsoft, Bitdefender and 360 as false positives.
What each file does: SECURITY.md.
```

**#200:** the same answer, shorter, with the first paragraph and the link to docs/INSTALL.md#install-antivirus.
Close both after posting (closing-issues template).

## Later: code signing

Signed files remove most of these warnings (SmartScreen, Defender's reputation). Options, all needing the owner's
identity and decision:

* **SignPath Foundation** (free code signing for open source projects): apply at https://signpath.org/ with the
  repository; they sign files produced by a GitHub Actions workflow they can check (it fits: releases are built only
  by `.github/workflows/build.yml`). The workflow then sends the setup and the DLLs to SignPath before
  `gh release create`.
* **Azure Artifact Signing** (Microsoft's paid service, formerly Trusted Signing): a monthly fee and an identity
  check; check whether individual developers in the owner's country are accepted.
* A classic OV certificate from a CA: paid yearly, a hardware key, usually needs a company or a notarised identity.

Until then the warnings come back with each release's new hashes; the submissions above are per file.
