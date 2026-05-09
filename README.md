Запускал через докер:

docker run --rm -v "$(pwd):/input:ro" gcc:latest bash -c "cp -r /input /work && cd /work && make && bash runme.sh && cat result.txt"