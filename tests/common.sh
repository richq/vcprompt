# where to find all the test files
testdir=`pwd`

# other globals set by functions here
vcprompt=""
tmpdir=""

find_vcprompt()
{
    vcprompt=$(dirname $(dirname $(readlink -f $0)))/vcprompt
    if [ ! -x $vcprompt ]; then
	echo "error: vcprompt executable not found (expected $vcprompt)" >&2
	exit 1
    fi
}

setup()
{
    tmpdir=`mktemp -d`
    trap cleanup 0 1 2 15
}

cleanup()
{
    chmod -R u+rwx $tmpdir
    rm -rf $tmpdir
}

assert_vcprompt()
{
    message=$1
    expect=$2
    format=$3
    if [ -z "$format" ]; then
        format="%b"
    fi

    if [ "$format" != '-' ]; then
        actual=`VCPROMPT_FORMAT="$VCPROMPT_FORMAT" $vcprompt -f "$format"`
    else
        actual=`VCPROMPT_FORMAT="$VCPROMPT_FORMAT" $vcprompt`
    fi

    if [ "$expect" != "$actual" ]; then
        echo "fail: $message: expected \"$expect\", but got \"$actual\"" >&2
        failed="y"
        return 1
    else
        echo "pass: $message"
    fi
}

report()
{
    if [ "$failed" ]; then
	echo "$0: some tests failed"
	exit 1
    else
	echo "$0: all tests passed"
	exit 0
    fi
}
