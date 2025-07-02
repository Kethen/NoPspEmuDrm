FROM ubuntu:22.04
RUN export DEBIAN_FRONTEND=noninteractive; apt update; apt install -y curl make git-core cmake python2 python3 sudo wget bzip2 xz-utils libreadline8 libusb-0.1-4 tmux libmpc3 mono-devel
RUN export VITASDK=/usr/local/vitasdk; export PATH=$VITASDK/bin:$PATH; git clone https://github.com/vitasdk/vdpm; cd vdpm; ./bootstrap-vitasdk.sh; ./install-all.sh
RUN echo 'export VITASDK=/usr/local/vitasdk' > /etc/profile.d/psvsdk.sh
RUN echo 'export PATH="$VITASDK/bin:$PATH"' >> /etc/profile.d/psvsdk.sh

RUN wget https://github.com/pspdev/pspdev/releases/download/v20230818/pspdev-ubuntu-latest.tar.gz -O - | gzip -d | tar -C /usr/local -x
RUN echo 'export PATH="/usr/local/pspdev/bin:$PATH"' > /etc/profile.d/pspsdk.sh
RUN echo 'export LD_LIBRARY_PATH="/usr/local/pspsdk/lib:$LD_LIBRARY_PATH"' >> /etc/profile.d/pspsdk.sh

RUN wget https://raw.githubusercontent.com/PSP-Archive/ARK-4/73c2dabf8ccf6c513c9dcd4a2356b0073d419489/contrib/PC/btcnf/btcnf.py -O /usr/bin/btcnf; chmod 755 /usr/bin/btcnf

ENTRYPOINT ["/bin/bash", "-l"]
