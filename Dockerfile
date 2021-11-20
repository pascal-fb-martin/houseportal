FROM debian:stable-slim
EXPOSE 80 70/udp
COPY . /
ENTRYPOINT [ "/usr/local/bin/houseportal" ]
CMD [ "--htpp-service=80" ]

