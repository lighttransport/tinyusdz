# TODO: set version from setup.py?

major_version=0
minor_version=8
micro_version=0
extra_revision="rc6"

version = "{}.{}.{}".format(major_version, minor_version, micro_version)

if isinstance(extra_revision, str):
    version += extra_revision

version_tag = "v" + version

