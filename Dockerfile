FROM debian:stable-slim
EXPOSE 80
EXPOSE 70/udp
COPY . /
ENTRYPOINT [ "/usr/local/bin/houseportal" ]
CMD [ "--htpp-service=80" ]

