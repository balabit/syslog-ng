FROM centos:7
ENV OS_PLATFORM centos-7

LABEL maintainer="Andras Mitzki <andras.mitzki@balabit.com>, Laszlo Szemere <laszlo.szemere@balabit.com>"

COPY helpers/* /helpers/

RUN /helpers/dependencies.sh install_yum_packages
RUN /helpers/dependencies.sh install_pip_packages

RUN /helpers/dependencies.sh install_criterion
RUN /helpers/dependencies.sh install_gradle
RUN /helpers/dependencies.sh install_gosu amd64

# add a fake sudo command
RUN mv /helpers/fake-sudo.sh /usr/bin/sudo


# mount points for source code
RUN mkdir /source
VOLUME /source
VOLUME /build


ENTRYPOINT ["/helpers/entrypoint.sh"]
