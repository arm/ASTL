# Release script plan

- prompt for implementing <https://jira.arm.com/browse/ASTL-301>, creating a installer/uninstaller

using codex cli and gpt-5.4

I want our users to have a smooth experience downloading and installing our tool.
Something simple, like
`curl https://github.com/Arm-Debug/ASTL/releases/download/release%2Frolling/get_astl.sh -sSf | sh`

This should download a release artifact from Github (later from Artifactory).
The '0.0.1' version might change, but for now the release artifact is a .zip hosted here: <https://github.com/Arm-Debug/ASTL/releases/download/release%2Frolling/astl_version_0.0.1_linux_aarch64_everything.zip>

The get_astl.sh script should accept some optional parameters, like:

- source (Github release page, internal artifactory (not implemented right now), and external artifactory (not implemented now)). Default should be github.
- version (default is latest)
- os and arch (default to whatever current system setup is)

After parsing arguments, I want get_astl.sh to do the following steps:

1. download the 'linux_aarch64_everything zip' (for now, but later, this would depend on the machine running the install script)
2. compare the .zip against a checksum (which needs to be added to `.github/workflows/create-release.yml`)
3. unzip it to a temporary dir and compare its contents to a manifest.json (also needs to be added in create-release.yml)

Then, the install.sh script contained in the .zip file should take over.

1. warn / error if package dependencies, like gnuplot, are missing.
2. copy lib files like libast-0.so and libastl_static-0.a into conventional library locations according to the manifest, depending on whether user is running as sudo or not
3. copy the header files in include/ into contentional locations according to manifest, depending on sudo
4. copy bin files like atx and Mocksysfs into conventional bin locations according to the manifest, depending on whether user is running as sudo or not
5. copy the config/ folder with json files into a destination based on the manifest and the 'Config file locations' order listed below.
6. copy the 'VERSION.md' file (needs to be added to .zip package in create-release.yml) into a place where an installer/uninstaller can see it
7. copy the manifest.json into a place where the uninstaller can find it.
8. put an uninstall script in an intuitive place where users could run it.

the uninstall script should make use of the manifest.json file to remove all files the installer set up.

I want this script to be intuitive and conventional. We might use it, or modify it to later build .deb packages, rpm packages, and .msi files to make the process more robust, so just keep that in mind.

## Config file locations

ASTL's `config` directory holds platform-specific metrics specifications
and should be included in distributions of the binary library.
ASTL looks for it in the following directories in preferred order:

1. Environment variable override: `ASTL_CONFIG_DIR`
2. Under a user-specific application data dir, depending on OS

Linux : `$XDG_DATA_HOME/astl/config` -> defaults to `~/.local/share/astl/config`
Mac : `~/Library/Application Support/astl/config`
Windows: `%LOCALAPPDATA%\astl\config` 3. System-wide application resource directory, depending on OS

Linux : `/usr/local/share/astl/config`
Mac : `/Library/Application Support/astl/config`
Windows: `%PROGRAMDATA%\astl\config` 4. Default to fallback of relative location by path to astl library.
For instance, if the library is at `/Downloads/astl/libastl.so`, ASTL looks in `/Downloads/astl/config/`
