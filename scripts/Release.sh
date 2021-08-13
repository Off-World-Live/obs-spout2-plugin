#!/bin/bash

set -e

echo ""
echo "*** Build Plugin for Release ***"
echo ""

SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
MSBUILD_PATH="c:/Program Files (x86)/Microsoft Visual Studio/2019/Community/MSBuild/Current/Bin/MSBuild.exe"
SOLUTION="$SCRIPTS_DIR/../../../build64/plugins/win-spout/win-spout.sln"
BUILD_ARGS="/target:Rebuild /property:Configuration=Release /maxcpucount:8 /verbosity:quiet /consoleloggerparameters:Summary;ErrorsOnly;WarningsOnly"

"$MSBUILD_PATH" "$SOLUTION" $BUILD_ARGS

echo ""
echo "*** Release Finished ***"
echo ""
