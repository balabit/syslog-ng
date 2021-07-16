## Syslog-ng development/release environment powered by Docker
With the help of the following tool you can
- compile syslog-ng from source code
- generate tarball (snapshot / release)
- create OS specific packages

in an isolated Docker container based environment.

## Usage information
```bash
whoami@host:~/syslog-ng$ dbld/rules [help]
```

dbld/rules is the general entrypoint for the tool, specifying multiple
targets.  For the complete list of the available targets please run the
command (without parameters it will run without any side effect), or read
the source code on [GitHub](rules).

Almost every `dbld/rules` command runs in a Docker container.  You can use
the pre-built containers from [DockerHub](https://hub.docker.com/u/balabit/)
or build your own images with the `dbld/rules image-<os>` command.

The source code and build products are mounted externally in the following locations:
- **/source** -> syslog-ng/*
- **/dbld** -> syslog-ng/dbld
- **/build** -> syslog-ng/dbld/build
- **/install** -> syslog-ng/dbld/install

## Examples

### Building syslog-ng from tarball using the 'tarball' image

```bash
$ dbld/rules tarball
$ Your tarball is in /build, also available on the host in $(top_srcdir)/dbld/build
$ cd dbld/build
$ tar -xzvf syslog-ng*.tar.gz
$ ./syslog-ng-*/configure
$ make
```

You can also build a DEB using:

```bash
$ dbld/rules deb-ubuntu-focal
```

You can find the resulting debs in `$HOME/syslog-ng/dbld/build`.

### Hacking on syslog-ng itself

You can also use the docker based shell to hack on syslog-ng by configuring
and building manually. You can use any of the supported OSes as shells (e.g.
shell-ubuntu-xenial or shell-centos-7) and there's "devshell" that contains
a few extra tools, often needed during development.

Steps for the manual build after entering into the containers shell:

```bash
$ ./dbld/rules shell-devshell

# inside the container
$ cd /source/
# autogen.sh generates the configure script using autotools, you could also
# use cmake (alternative build system, experimental) here.
$ ./autogen.sh
$ cd /build/
# run the configure script, there's a wrapper for this in /dbld/bootstrap
# that will include extra options exported by dbld/rules.
$ /source/configure --enable-debug --prefix=/install
$ make
$ make check
$ make install
```

If the compilation and installation was successful you can run syslog-ng with the following command:

```bash
$ /install/syslog-ng/sbin/syslog-ng -Fedv
```

### Preparing a release

The dbld tools also allow to prepare for a syslog-ng release. By default,
when you generate a tarball/deb/rpm, dbld would generate a "snapshot"
version (including a git commit id) to avoid mismatching it with a "real"
release.

If you instead want to do an "official-looking" release (e.g.  3.28.1
instead of 3.27.1.34.g83096fa.dirty), this is the procedure you have to
follow.  The syslog-ng team is using this to perform releases.

### Version bumps

First of all, you will need to commit a patch that bumps the version number
in a couple of places (e.g. the VERSION file in the root directory).

Start with a git commit that you want to release (e.g the master branch),
with no local changes.

Version bumps are automated using the "prepare-release" target of
dbld that can be invoked like this:

```bash
$ ./dbld/rules prepare-release VERSION=3.28.1
```

To see what prepare-release does automatically consult the script
`dbld/prepare-release` in the source tree.

`prepare-release` does not commit the changes, rather it leaves them for you
to review and then commit.

### Performing a release

Once the versions are bumped, that change is committed, the source tree is
prepared for a release. You can do it via:

```bash
$ ./dbld/rules release VERSION=3.28.1
```

This will build:
  * a tarball,
  * automatically generates an up-to-date rpm/debian packaging,
  * build rpm/deb packages on their default platform
  * tag the current commit in git

All artifacts (tarball, deb/rpm packages) are stored in `./dbld/release/<VERSION>`

The script does not undertake publishing the artifacts or the tag in any
way. It is expected that some kind of CI system will perform this (jenkins,
travis, github-actions).

## sudo
The `sudo` command is not available inside this development container.

>Short explanation:
>
>Many of the image maintainers (mostly for security reasons) do not install the sudo package by default. Additionally (to avoid access problems to your repository outside of this container), we
>- mount directories
>- run commands
>
>inside this container using your external Username and ID.

There are many options to circumvent this limitation (i.e. Create your own image, based on this one.), but probably the easiest way is to start a new privileged shell in the already running container, using `docker exec`.
```bash
$ docker exec -it <container-name or ID> /bin/bash
```

> note: We installed a fake `sudo` command inside the container, which will print out a copy-paste ready version of the `docker exec` command in case someone accidentally calls it.
