#!/bin/bash
set -e

CUR_DIR=$(cd $(dirname $0);pwd)
TORCH_MUSA_HOME=$CUR_DIR
PATCHES_DIR=${TORCH_MUSA_HOME}/torch_patches/
PYTORCH_PATH=${TORCH_MUSA_HOME}/../pytorch  # default pytorch repo path

BUILD_WHEEL=0
FORCE_PATCH=0
DEBUG_MODE=0
ASAN_MODE=0
BUILD_TORCH=1
BUILD_TORCH_MUSA=1
ONLY_PATCH=0
CLEAN=0

usage() {
  echo -e "\033[1;32mThis script is used to build PyTorch and Torch_MUSA. \033[0m"
  echo -e "\033[1;32mParameters usage: \033[0m"
  echo -e "\033[32m    --all         : Means building both PyTorch and Torch_MUSA. \033[0m"
  echo -e "\033[32m    -m/--musa     : Means building Torch_MUSA only. \033[0m"
  echo -e "\033[32m    -t/--torch    : Means building original PyTorch only. \033[0m"
  echo -e "\033[32m    -d/--debug    : Means building in debug mode. \033[0m"
  echo -e "\033[32m    -a/--asan     : Means building in asan mode. \033[0m"
  echo -e "\033[32m    -c/--clean    : Means cleaning everything that has been built. \033[0m"
  echo -e "\033[32m    -p/--patch    : Means applying patches only. \033[0m"
  echo -e "\033[32m    -w/--wheel    : Means generating wheel after building. \033[0m"
  echo -e "\033[32m    --force-patch : Means applying patches in any case. \033[0m"
  echo -e "\033[32m    -h/--help     : Help information. \033[0m"
}

# parse paremters
parameters=`getopt -o +mtdacpwh --long all,musa,torch,debug,asan,clean,patch,wheel,force-patch,help, -n "$0" -- "$@"`
[ $? -ne 0 ] && { echo -e "\033[34mTry '$0 --help' for more information. \033[0m"; exit 1; }

eval set -- "$parameters"

while true;do
    case "$1" in
        --all) BUILD_TORCH=1; BUILD_TORCH_MUSA=1; shift ;;
        -m|--musa) BUILD_TORCH_MUSA=1; BUILD_TORCH=0; shift ;;
        -t|--torch) BUILD_TORCH_MUSA=0; BUILD_TORCH=1; shift ;;
        -d|--debug) DEBUG_MODE=1; shift ;;
        -a|--asan) ASAN_MODE=1; shift ;;
        -c|--clean) CLEAN=1; shift ;;
        -w|--wheel) BUILD_WHEEL=1; shift ;;
        -p|--patch) ONLY_PATCH=1; shift ;;
        --force-patch) FORCE_PATCH=1; shift ;;
        -h|--help) usage;exit ;;
        --)
            shift ; break ;;
        *) usage;exit 1;;
    esac
done

clone_pytorch() {
  # if PyTorch repo exists already, we skip gitting clone PyTorch
  if [ ! -z ${PYTORCH_REPO_PATH} ]; then
    PYTORCH_PATH=${PYTORCH_REPO_PATH}
    echo -e "\033[34mPyTorch repo path is ${PYTORCH_PATH} ...\033[0m"
  else
    ABSOLUTE_PATH=`cd $(dirname ${PYTORCH_PATH}) && pwd`"/pytorch"
    echo -e "\033[34mUsing default pytorch repo path: ${ABSOLUTE_PATH}\033[0m"
    if [ ! -d "${PYTORCH_PATH}" ]; then
      pushd ${TORCH_MUSA_HOME}/..
      echo -e "\033[34mPyTorch repo does not exist, now git clone PyTorch to ${ABSOLUTE_PATH} ...\033[0m"
      git clone -b v1.12.0 https://github.com/pytorch/pytorch.git --depth=1
      popd 
    fi
  fi
}

apply_patches() {
  # check if patching have been done
  if [ -f "${PYTORCH_PATH}/c10/core/DeviceType.h" ]; then
    mtgpu_patch_flag=`(grep -rnIi "DeviceType::MTGPU" $PYTORCH_PATH/c10/core/DeviceType.h || true)`
    if [ "$mtgpu_patch_flag"x != ""x ]; then
      if [ ${FORCE_PATCH} -eq 0 ]; then
        echo -e "\033[34mInfo: patches have been applied to Pytorch already. Skip patching! \033[0m"
        return
      fi
    fi
  fi

  # apply patches into PyTorch
  echo -e "\033[34mApplying patches to ${PYTORCH_PATH} ...\033[0m"
  # clean PyTorch before patching
  if [ -d "$PYTORCH_PATH/.git" ]; then
    echo -e "\033[34mCleaning the PyTorch environment before patching. \033[0m"
    pushd $PYTORCH_PATH
    git reset --hard
    popd
  fi
  
  patch_args="-p 1 -E -l -N -r -s --no-backup-if-mismatch"
  for file in `ls -a $PATCHES_DIR`
  do
    if [ "${file##*.}"x = "patch"x ]; then
      echo -e "\033[34mapplying patch: $file \033[0m"
      patch -d $PYTORCH_PATH -i $PATCHES_DIR/$file $patch_args
    fi
  done
}

build_pytorch() {
  if [ ! -d ${PYTORCH_PATH} ]; then
    echo -e "\033[34mAn error occurred while building PyTorch, the specified PyTorch \
             repo ${PYTORCH_PATH} does not exist \033[0m"
    exit 1
  fi

  pushd ${PYTORCH_PATH}
  pip install -r requirements.txt
  pip install -r ${TORCH_MUSA_HOME}/requirements.txt  # extra requirements
  if [ $BUILD_WHEEL -eq 1 ]; then
    rm -rf dist
    DEBUG=${DEBUG_MODE} USE_ASAN=${ASAN_MODE} USE_MKL=1 USE_MKLDNN=1 USE_MKLDNN_CBLAS=1 python setup.py bdist_wheel
    rm -rf torch.egg-info
    pip install dist/*.whl
  else
    DEBUG=${DEBUG_MODE} USE_ASAN=${ASAN_MODE} USE_MKL=1 USE_MKLDNN=1 USE_MKLDNN_CBLAS=1 python setup.py install
  fi

  popd
}

clean_pytorch() {
  pushd ${PYTORCH_REPO_PATH}
  python setup.py clean
  popd
}

clean_torch_musa() {
  pushd ${TORCH_MUSA_HOME}
  python setup.py clean
  popd
}
build_torch_musa() {
  pushd ${TORCH_MUSA_HOME}
  if [ $BUILD_WHEEL -eq 1 ]; then
    rm -rf dist
    PYTORCH_REPO_PATH=${PYTORCH_PATH} DEBUG=${DEBUG_MODE} USE_ASAN=${ASAN_MODE} python setup.py bdist_wheel
    rm -rf torch_musa.egg-info
    pip install dist/*.whl
  else
    PYTORCH_REPO_PATH=${PYTORCH_PATH} DEBUG=${DEBUG_MODE} USE_ASAN=${ASAN_MODE} python setup.py install
  fi
  popd
}

main() {
  if [ ${CLEAN} -eq 1 ]; then
    clean_pytorch
    clean_torch_musa
    exit 0
  fi
  if [ ${ONLY_PATCH} -eq 1 ]; then
    apply_patches
    exit 0
  fi
  if [ ${BUILD_TORCH} -eq 1 ]; then
    clone_pytorch
    apply_patches
    build_pytorch
  fi
  if [ ${BUILD_TORCH_MUSA} -eq 1 ]; then
    build_torch_musa
  fi
}

main
