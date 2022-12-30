#!/bin/sh
# Copyright (c) 2019  LG Electronics, Inc.

arm_libtool=`find -name "libtool"`
echo "Found ${arm_libtool}"

line=`grep -n "func_resolve_sysroot_result=\$\lt_sysroot\$\func_stripname_result" ./${arm_libtool} | cut -d':' -f1`

my_ltsysroot="if [[ "\$func_stripname_result" =~ "\$lt_sysroot" ]]; then func_resolve_sysroot_result=\$func_stripname_result; else func_resolve_sysroot_result=\$lt_sysroot\$func_stripname_result; fi"

if [ "${line}+99999" = "+99999" ]; then
  echo "Nothing to do for ${arm_libtool}"
else
  sed -i "${line}s/.*/${my_ltsysroot}/g" ${arm_libtool}
  echo "Updated sysroot in ${arm_libtool}"
fi

found=`grep -c ${SDKTARGETSYSROOT}${SDKTARGETSYSROOT} Makefile`
# found=`grep -c "'"${mysysroot}"''"${mysysroot}"'" Makefile`
if [ "${found}" = "0" ]; then
  echo "No Found duplicated sysroot"
else
  echo "Found duplicated sysroot"
  mysysroot=`echo ${SDKTARGETSYSROOT} | sed -e 's/\//\\\\\//g'`
  find -name "*Makefile" -exec sed -i 's/'"${mysysroot}"''"${mysysroot}"'/'"${mysysroot}"'/g' {} \;
  echo "Updated sysroot in all Makefile(s)"
fi
