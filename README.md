# mtuci-os-project


## Linux


# Собираем образ
docker build -t linux_client .

# Запускаем контейнер, пробрасывая хостовую сеть
docker run -it --rm --network host linux_client 


## Windows

# Собираем образ
docker build -t windows_client  -f Dockerfile_for_Windows .

# Запускаем контейнер
docker run -it --rm windows_client