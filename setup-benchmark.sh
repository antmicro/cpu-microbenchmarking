#!/usr/bin/env bash
set -eu

# Import configuration
. config.sh

function get_repo_url() {
    local repo="$1"

    echo "https://github.com/$repo.git"
}

# Gets which version (i.e. SHA) of a given submodule is referenced by a given repository.
function find_sha_of_submodule() {
    repo=$1
    branch=$2
    sha=$3
    repo_dir_name=$4
    submodule=$5

    # Shallow clone repo, with no files.
    git clone -n --depth=1 --filter=tree:0 "${repo}" -b "${branch}" "${repo_dir_name}" > /dev/null
    cd "$repo_dir_name"

    # Checkout the renode submodule.
    git sparse-checkout set --no-cone /${submodule}/ > /dev/null
    git checkout > /dev/null
    
    # Get the SHA of the renode submodule
    escaped_submodule="${submodule//\//\\\/}" # Replaces / with \/
    submodule_sha=$(git submodule status --cached | grep -Po "(?<=\-)[^\ ]*(?=\ ${escaped_submodule})") # matches exactly the sha of the submodule

    # Exit $repo_dir_name
    cd ..

    # Remove $repo_dir_name
    rm -rf "./${repo_dir_name}/"

    # Return the result
    echo ${submodule_sha}
}

# Clones a specific tlib commit derived from the submodule hierarchy of the given hq commit.
function clone_tlib_from_ref() {
    ref=$1 # format is branch@sha
    branch="${ref%@*}"
    sha="${ref#*@}"
    sha_short=$(git rev-parse --short=5 ${sha})

    echo branch is $branch
    echo sha is $sha
    echo sha_short is $sha_short

    renode_dir_name="renode@${sha_short}"

    renode_repo=$(get_repo_url "renode/renode")
    infra_sha=$(find_sha_of_submodule "${renode_repo}" ${branch} ${sha} ${renode_dir_name} "src/Infrastructure")
    infra_sha_short=$(git rev-parse --short=5 ${infra_sha})
    infra_dir_name="infrastructure@${infra_sha_short}"

    infra_repo=$(get_repo_url "renode/renode-infrastructure")
    tlib_sha=$(find_sha_of_submodule "${infra_repo}" ${branch} ${infra_sha} ${infra_dir_name} "src/Emulator/Cores/tlib")

    # Clone tlib
    tlib_repo=$(get_repo_url "antmicro/tlib")
    tlib_dir_name="tlib_${branch}@${sha_short}"
    git clone --recurse-submodules --depth 1 "${tlib_repo}" -b ${branch} ${tlib_dir_name}
}

if [ $0 != "./setup-benchmark.sh" ] ; then
    exit 0
fi 

#
# Filter which hq refs to use.
#
branch_regex=${1:-".*"}

filtered_refs=$(
    for ref in ${refs[@]}; do 
        sha="${ref#*@}" 
        branch_name="${ref%@*}" # Assuming branches are named the same in every submodule.
        fully_qualified_branch="${branch_name}@${sha}"

        if [[ $fully_qualified_branch =~ $branch_regex ]]; then 
            echo $fully_qualified_branch
        else 
            continue
        fi
    done
)

#
# Set up all tlib branches under test.
#
for ref in ${filtered_refs[@]}; do 
    sha="${ref#*@}" 
    sha_short=$(git rev-parse --short=5 ${sha})
    tlib_branch_name="${ref%@*}" # Assuming branches are named the same in every submodule.
    tlib_folder="tlib_${tlib_branch_name}@${sha_short}"

    # Only clone if not already cloned.
    if [ ! -d ./${tlib_folder} ]; then 
        # Clone into unique folder.
        clone_tlib_from_ref $ref

        # Apply patches necessary for cpu-microbenchmark to work.
        cd ${tlib_folder}
        for patch in ../patches/*.patch; do
            git apply --reject --whitespace=fix $patch || true
        done
        cd ..
        
    fi

    # Build tlib.
    mkdir -p ${tlib_folder}/build
    cd ${tlib_folder}/build
    cmake -DTARGET_ARCH=${target_arch} -DCMAKE_BUILD_TYPE=${build_type} -DTARGET_WORD_SIZE=${target_word_size} ..
    make -j$(nproc)
    cd ../..
done 

#
# Set up cpu-benchmark
#
for ref in ${filtered_refs[@]}; do 
    sha="${ref#*@}" 
    sha_short=$(git rev-parse --short=5 ${sha})
    branch="${ref%@*}" 
    ./make.sh ${branch}@${sha_short}
done 
