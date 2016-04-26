#!/bin/bash

for f in tests-3d/test-*.c; do
	modname=`basename $f`
	modname=${modname%.c}
	grep GLES3 $f > /dev/null && gles="GLESv3" || gles="GLESv2"

	cat << EOF

include \$(CLEAR_VARS)
LOCAL_MODULE    := $modname
LOCAL_SRC_FILES := $f
LOCAL_C_INCLUDES := \$(LOCAL_PATH)/includes \$(LOCAL_PATH)/util
LOCAL_CFLAGS := -DBIONIC -std=c99
LOCAL_LDLIBS := -llog -lc -ldl -lEGL -l$gles
include \$(BUILD_EXECUTABLE)
EOF
done
