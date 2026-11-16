Import("env")

import os


def _find_node_dir():
    import shutil

    p = shutil.which("npm") or shutil.which("npm.cmd")
    if p:
        return os.path.dirname(os.path.abspath(p))

    # Fallback: bundled Node.js runtimes
    import glob
    patterns = [
        os.path.join(os.path.expanduser("~"), ".workbuddy", "binaries",
                     "node", "versions", "*", "npm.cmd"),
        r"C:\Program Files\nodejs\npm.cmd",
    ]
    for pat in patterns:
        hits = glob.glob(pat)
        if hits:
            return os.path.dirname(os.path.abspath(hits[0]))
    return None


_node_dir = _find_node_dir()
if _node_dir:
    env.PrependENVPath("PATH", _node_dir)
else:
    print("WARNING: npm not found; web page build will fail. "
          "Install Node.js or add it to PATH.")

package_json = env.File("package.json")

#
# automatically generate `include/version.h`
#
def build_version_h(target, source, env):
    import json

    version = None
    with open(str(source[0]), "r") as f:
        version = json.load(f)["version"]

    with open(str(target[0]), "w") as h:
        h.write('// Automatically generated -- do not modify\n')
        h.write('// Modify `package.json` instead.\n\n')
        h.write('#define VERSION "' + version + '"\n')

    return None

#
# automatically generate `include/ClockType.h`
#
def build_clock_type_h(target, source, env):
    import glob

    with open(str(target[0]), "w") as h:
        h.write('// Automatically generated -- do not modify\n\n')
        for file in glob.glob("include/WordClockTypes/*.hpp"):
            h.write('#include "' + file.replace('include/','') + '"\n')

    return None

env.Command(
    target="include/ClockType.gen.h",
    source=package_json,
    action=build_clock_type_h
)

env.Command(
    target="include/version.gen.h",
    source=package_json,
    action=build_version_h
)

#
# automatically build web page
#

# npm ci deletes node_modules before reinstalling, which some file protection
# setups refuse (and then the build fails after the packages are already gone).
# npm install still honours package-lock.json but only adds what is missing.
npm_ci = env.Command(
    target="node_modules/.package-lock.json",
    source="package-lock.json",
    action="npm install --silent"
)

env["ENV"]["PIO_ENV_NAME"] = env.subst("$PIOENV")

build_dir = env.subst("$BUILD_DIR")
web_page_content = os.path.join(build_dir, "WebPageContent.gen.inc")


def for_grunt(path):
    try:
        path = os.path.relpath(path, env.subst("$PROJECT_DIR"))
    except ValueError:
        pass
    return path.replace(os.sep, "/")


env["ENV"]["PIO_WEBPAGE_TARGET"] = for_grunt(web_page_content)
env["ENV"]["PIO_WEBPAGE_TEMP"] = for_grunt(os.path.join(build_dir, "webpage"))

env.Prepend(CPPPATH=[build_dir])

legacy = os.path.join("include", "WebPageContent.gen.inc")
if os.path.exists(legacy):
    os.remove(legacy)

grunt_build = env.Command(
    target=web_page_content,
    source="Gruntfile.js",
    action="npx --no-install grunt build"
)
env.Depends(grunt_build, npm_ci)
env.Depends(grunt_build, package_json)
env.Depends(grunt_build, env.Glob("webpage/*"))
# Subdirectories too: a glob on "webpage/*" only tracks the directory node, so
# editing an existing icon or language file would not trigger a rebuild -- only
# adding or removing one changes the directory's timestamp.
env.Depends(grunt_build, env.Glob("webpage/icons/*"))
env.Depends(grunt_build, env.Glob("webpage/language/*"))
