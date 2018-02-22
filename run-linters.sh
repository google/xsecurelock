#!/bin/bash

# FlawFinder.
if which flawfinder; then
  flawfinder .
fi

# CPPCheck.
if which cppcheck; then
  cppcheck --enable=all --inconclusive --std=posix  .
fi

# OCLint.
if which oclint; then
  # Try once without extensions.
  oclint -enable-clang-static-analyzer -enable-global-analysis \
    -disable-rule=AvoidBranchingStatementAsLastInLoop \
    -disable-rule=BitwiseOperatorInConditional \
    -disable-rule=CollapsibleIfStatements \
    -disable-rule=DeepNestedBlock \
    -disable-rule=GotoStatement \
    -disable-rule=HighCyclomaticComplexity \
    -disable-rule=HighNcssMethod \
    -disable-rule=HighNPathComplexity \
    -disable-rule=InvertedLogic \
    -disable-rule=LongMethod \
    -disable-rule=LongVariableName \
    -disable-rule=ParameterReassignment \
    -disable-rule=ShortVariableName \
    -disable-rule=TooFewBranchesInSwitchStatement \
    -disable-rule=UselessParentheses \
    -extra-arg=-DHELPER_PATH=\"\" \
    -extra-arg=-DDOCS_PATH=\"\" \
    -extra-arg=-DAUTH_EXECUTABLE=\"\" \
    -extra-arg=-DGLOBAL_SAVER_EXECUTABLE=\"\" \
    -extra-arg=-DSAVER_EXECUTABLE=\"\" \
    -extra-arg=-DPAM_SERVICE_NAME=\"\" \
    *.[ch] */*.[ch]
  # Try again with all extensions.
  oclint -enable-clang-static-analyzer -enable-global-analysis \
    -disable-rule=AvoidBranchingStatementAsLastInLoop \
    -disable-rule=BitwiseOperatorInConditional \
    -disable-rule=CollapsibleIfStatements \
    -disable-rule=DeepNestedBlock \
    -disable-rule=GotoStatement \
    -disable-rule=HighCyclomaticComplexity \
    -disable-rule=HighNcssMethod \
    -disable-rule=HighNPathComplexity \
    -disable-rule=InvertedLogic \
    -disable-rule=LongMethod \
    -disable-rule=LongVariableName \
    -disable-rule=ParameterReassignment \
    -disable-rule=ShortVariableName \
    -disable-rule=TooFewBranchesInSwitchStatement \
    -disable-rule=UselessParentheses \
    -extra-arg=-DHELPER_PATH=\"\" \
    -extra-arg=-DDOCS_PATH=\"\" \
    -extra-arg=-DAUTH_EXECUTABLE=\"\" \
    -extra-arg=-DGLOBAL_SAVER_EXECUTABLE=\"\" \
    -extra-arg=-DSAVER_EXECUTABLE=\"\" \
    -extra-arg=-DPAM_SERVICE_NAME=\"\" \
    -extra-arg=-DHAVE_SCRNSAVER \
    -extra-arg=-DHAVE_COMPOSITE \
    -extra-arg=-DHAVE_XRANDR \
    -extra-arg=-DHAVE_XKB \
    *.[ch] */*.[ch]
fi

# Clang Analyzer.
if which scan-build; then
  make clean
  scan-build make
fi

# Build for Coverity Scan.
if which cov-build; then
  make clean
  rm -rf cov-int
  cov-build --dir cov-int make
  tar cvjf cov-int.tbz2 cov-int/
  rm -rf cov-int
  rev=$(git describe --always --dirty)
  curl --form token="$COVERITY_TOKEN" \
    --form email="$COVERITY_EMAIL" \
    --form file=@cov-int.tbz2 \
    --form version="$rev" \
    --form description="$rev" \
    https://scan.coverity.com/builds?project=xsecurelock
  rm -f cov-int.tbz2
fi
