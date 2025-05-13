# mtuci-os-project


## Linux


# Собираем образ
docker build -t c-client .

# Запускаем контейнер, пробрасывая хостовую сеть
docker run -it --rm --network host c-client


## Windows

# Узнать адрес хоста из контейнера
docker run --rm alpine sh -c "ping -c1 host.docker.internal"


const char *host = (argc > 1 ? argv[1] : "host.docker.internal");
