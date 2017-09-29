FROM platina-buildroot

RUN rm -rf builds/*/build/linux-master/build && rm -f builds/*/build/linux-master/.stamp_built builds/*/build/linux-master/.stamp_target_installed builds/*/build/linux-master/.stamp_staging_installed

COPY . builds/example-amd64/build/linux-master
COPY . builds/platina-mk1/build/linux-master
COPY . builds/platina-mk1-bmc/builds/linux-master

RUN make -C buildroot O=../builds/example-amd64 example-amd64_defconfig && make -C builds/example-amd64 all
RUN make -C buildroot O=../builds/platina-mk1 platina-mk1_defconfig && make -C builds/platina-mk1 all
RUN make -C buildroot O=../builds/platina-mk1-bmc platina-mk1-bmc_defconfig && make -C builds/platina-mk1-bmc all
