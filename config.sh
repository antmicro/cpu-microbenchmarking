#!/usr/bin/env bash

refs=(
    "master@63e57ff18ef47b0171d3a191e317652691a0b988"
    "always-go-through-softmmu-qemu-st@89867b3be92d724e271c5ca55b76427eaac95294"
    "public-hst-weak-ir@363512a385295f055b0055cf2b6924b7b54b97e5"
    "public-hst-strong-ir@16d211871440773e7e2c0cb46433947836ab319d"
    "public-hst-weak-helpers@17e0b80e0519020e4ce1addafbf5c4d9daf14e39"
    "public-hst-strong-helpers@1bd004d0e04807f2e988e178a064c1979814363c"
)

target_arch=riscv
target_word_size=64
build_type=Release
