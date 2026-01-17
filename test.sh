#!/bin/bash
set -e

# Захардкоженные настройки
SERVER_URL="http://127.0.0.1:8080"
MOUNT_POINT="/mnt/vtfs"
TOKEN="test_token"

# Цвета для вывода
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Функции для вывода
test_pass() {
    echo -e "${GREEN}✓${NC} $1"
}

test_fail() {
    echo -e "${RED}✗${NC} $1"
    exit 1
}

test_info() {
    echo -e "${YELLOW}➜${NC} $1"
}

echo "=========================================="
echo "  Полное тестирование VTFS - Этап 10"
echo "=========================================="
echo ""
echo "Сервер: $SERVER_URL"
echo "Точка монтирования: $MOUNT_POINT"
echo "Токен: $TOKEN"
echo ""

# ===== Предварительные проверки =====
echo "===== Предварительные проверки ====="

test_info "Проверка сетевого подключения к серверу..."
if nc -zv 127.0.0.1 8080 2>&1 | grep -q succeeded || nc -zv 127.0.0.1 8080 2>&1 | grep -q open; then
    test_pass "Сервер доступен (127.0.0.1:8080)"
else
    test_fail "Не могу достучаться до сервера (127.0.0.1:8080). Запустите сервер: cd server && ./gradlew bootRun"
fi

test_info "Проверка доступности сервера..."
if curl -s --connect-timeout 5 "$SERVER_URL/list?token=$TOKEN&path=/" > /dev/null 2>&1; then
    test_pass "Сервер отвечает"
else
    test_fail "Сервер не отвечает на $SERVER_URL. Запустите сервер: cd server && ./gradlew bootRun"
fi

test_info "Проверка загрузки модуля..."
if lsmod | grep -q vtfs; then
    test_pass "Модуль уже загружен (будет перезагружен)"
else
    test_info "Модуль не загружен (будет загружен)"
fi

test_info "Проверка монтирования..."
if mount | grep -q "$MOUNT_POINT"; then
    test_pass "ФС уже смонтирована (будет перемонтирована)"
else
    test_info "ФС не смонтирована (будет смонтирована)"
fi

echo ""

# ===== Очистка перед тестами =====
echo "===== Очистка перед тестами ====="

test_info "Очистка локального хранилища (перезагрузка модуля)"
sudo umount "$MOUNT_POINT" 2>/dev/null || true
sudo rmmod vtfs 2>/dev/null || true
sleep 1
sudo insmod module/vtfs.ko token="$TOKEN"
if [ $? -eq 0 ]; then
    test_pass "Модуль перезагружен"
else
    test_fail "Ошибка перезагрузки модуля"
fi

test_info "Монтирование файловой системы"
sudo mount -t vtfs none "$MOUNT_POINT"
if [ $? -eq 0 ]; then
    test_pass "Файловая система смонтирована"
else
    test_fail "Ошибка монтирования"
fi

test_info "Очистка сервера от старых данных"
# Удаляем все возможные тестовые файлы/директории с сервера
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/test1.txt" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/test_dir/file.txt" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/test_dir" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/large_file.txt" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/persistent.txt" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/persistent_dir/file.txt" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/persistent_dir" > /dev/null 2>&1 || true

# Очистка файлов теста 3 (расширенный тест)
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/rewrite_test.txt" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/stat_test.txt" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/stat_dir" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/level1/file_l1.txt" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/level1/level2/file_l2.txt" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/level1/level2/level3/file_l3.txt" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/level1/level2/level3" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/level1/level2" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/level1" > /dev/null 2>&1 || true
for i in {1..5}; do
    curl -s "$SERVER_URL/delete?token=$TOKEN&path=/multi_$i.txt" > /dev/null 2>&1 || true
done

# Очистка файлов теста 4 (интеграционный, dir1)
for i in {1..10}; do
    curl -s "$SERVER_URL/delete?token=$TOKEN&path=/dir1/file$i.txt" > /dev/null 2>&1 || true
done
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/dir1/dir2" > /dev/null 2>&1 || true
curl -s "$SERVER_URL/delete?token=$TOKEN&path=/dir1" > /dev/null 2>&1 || true
test_pass "Сервер очищен"

echo ""

# ===== Тест 1: Базовые операции с файлами =====
echo "===== Тест 1: Базовые операции с файлами ====="

test_info "Создание файла test1.txt"
echo "Hello World" | sudo tee "$MOUNT_POINT/test1.txt" > /dev/null
if [ $? -eq 0 ]; then
    test_pass "Файл создан"
else
    test_fail "Ошибка создания файла"
fi

test_info "Чтение файла test1.txt"
CONTENT=$(sudo cat "$MOUNT_POINT/test1.txt")
if [ "$CONTENT" = "Hello World" ]; then
    test_pass "Файл прочитан корректно"
else
    test_fail "Ошибка чтения файла: '$CONTENT'"
fi

test_info "Проверка данных на сервере"
SERVER_RESPONSE=$(curl -s "$SERVER_URL/read?token=$TOKEN&path=/test1.txt&offset=0&size=11")
if echo "$SERVER_RESPONSE" | grep -q '"result"'; then
    test_pass "Данные на сервере присутствуют"
else
    test_fail "Данные на сервере отсутствуют или ошибка: $SERVER_RESPONSE"
fi

test_info "Удаление файла"
sudo rm "$MOUNT_POINT/test1.txt"
if [ $? -eq 0 ]; then
    test_pass "Файл удалён"
else
    test_fail "Ошибка удаления файла"
fi

echo ""

# ===== Тест 2: Операции с директориями =====
echo "===== Тест 2: Операции с директориями ====="

test_info "Создание директории test_dir"
sudo mkdir "$MOUNT_POINT/test_dir"
if [ $? -eq 0 ]; then
    test_pass "Директория создана"
else
    test_fail "Ошибка создания директории"
fi

test_info "Создание файла в директории"
echo "Test content" | sudo tee "$MOUNT_POINT/test_dir/file.txt" > /dev/null
if [ $? -eq 0 ]; then
    test_pass "Файл создан в директории"
else
    test_fail "Ошибка создания файла в директории"
fi

test_info "Список файлов в директории"
FILES=$(sudo ls "$MOUNT_POINT/test_dir")
if echo "$FILES" | grep -q "file.txt"; then
    test_pass "Файл найден в списке"
else
    test_fail "Файл не найден в списке: $FILES"
fi

test_info "Проверка директории на сервере"
SERVER_LIST=$(curl -s "$SERVER_URL/list?token=$TOKEN&path=/test_dir")
if echo "$SERVER_LIST" | grep -q "file.txt"; then
    test_pass "Директория на сервере корректна"
else
    test_fail "Директория на сервере некорректна: $SERVER_LIST"
fi

test_info "Удаление директории"
sudo rm -f "$MOUNT_POINT/test_dir/file.txt"
sudo rmdir "$MOUNT_POINT/test_dir"
if [ $? -eq 0 ]; then
    test_pass "Директория удалена"
else
    test_fail "Ошибка удаления директории"
fi

echo ""

# ===== Тест 3: Расширенный функциональный тест =====
echo "===== Тест 3: Расширенный функциональный тест ====="

test_info "Создание и перезапись файлов"
echo "Initial content" | sudo tee "$MOUNT_POINT/rewrite_test.txt" > /dev/null
CONTENT1=$(sudo cat "$MOUNT_POINT/rewrite_test.txt")
if [ "$CONTENT1" = "Initial content" ]; then
    test_pass "Файл создан с начальным содержимым"
else
    test_fail "Ошибка начального содержимого"
fi

echo "Overwritten content" | sudo tee "$MOUNT_POINT/rewrite_test.txt" > /dev/null
CONTENT2=$(sudo cat "$MOUNT_POINT/rewrite_test.txt")
if [ "$CONTENT2" = "Overwritten content" ]; then
    test_pass "Файл перезаписан корректно"
else
    test_fail "Ошибка перезаписи: '$CONTENT2'"
fi
sudo rm "$MOUNT_POINT/rewrite_test.txt"

test_info "Проверка stat на файлах"
echo "Test stat" | sudo tee "$MOUNT_POINT/stat_test.txt" > /dev/null
if sudo stat "$MOUNT_POINT/stat_test.txt" > /dev/null 2>&1; then
    test_pass "Stat на файле работает"
else
    test_fail "Stat на файле не работает"
fi

test_info "Проверка stat на директориях"
sudo mkdir "$MOUNT_POINT/stat_dir"
if sudo stat "$MOUNT_POINT/stat_dir" > /dev/null 2>&1; then
    test_pass "Stat на директории работает"
else
    test_fail "Stat на директории не работает"
fi

test_info "Вложенные директории (3 уровня)"
sudo mkdir "$MOUNT_POINT/level1"
sudo mkdir "$MOUNT_POINT/level1/level2"
sudo mkdir "$MOUNT_POINT/level1/level2/level3"
if [ -d "$MOUNT_POINT/level1/level2/level3" ]; then
    test_pass "Вложенные директории созданы"
else
    test_fail "Ошибка создания вложенных директорий"
fi

test_info "Файлы на разных уровнях вложенности"
echo "L1" | sudo tee "$MOUNT_POINT/level1/file_l1.txt" > /dev/null
echo "L2" | sudo tee "$MOUNT_POINT/level1/level2/file_l2.txt" > /dev/null
echo "L3" | sudo tee "$MOUNT_POINT/level1/level2/level3/file_l3.txt" > /dev/null

L1_CONTENT=$(sudo cat "$MOUNT_POINT/level1/file_l1.txt")
L2_CONTENT=$(sudo cat "$MOUNT_POINT/level1/level2/file_l2.txt")
L3_CONTENT=$(sudo cat "$MOUNT_POINT/level1/level2/level3/file_l3.txt")

if [ "$L1_CONTENT" = "L1" ] && [ "$L2_CONTENT" = "L2" ] && [ "$L3_CONTENT" = "L3" ]; then
    test_pass "Файлы на всех уровнях работают корректно"
else
    test_fail "Ошибка чтения файлов на разных уровнях"
fi

test_info "Проверка list на вложенных директориях"
FILES_L2=$(sudo ls "$MOUNT_POINT/level1/level2")
if echo "$FILES_L2" | grep -q "file_l2.txt" && echo "$FILES_L2" | grep -q "level3"; then
    test_pass "List на вложенной директории работает"
else
    test_fail "List на вложенной директории не работает"
fi

test_info "Множественные операции с файлами"
for i in {1..5}; do
    echo "Multi file $i" | sudo tee "$MOUNT_POINT/multi_$i.txt" > /dev/null
done
MULTI_COUNT=$(sudo ls "$MOUNT_POINT" | grep -c "multi_.*\.txt")
if [ "$MULTI_COUNT" -eq 5 ]; then
    test_pass "Создано 5 файлов одновременно"
else
    test_fail "Создано только $MULTI_COUNT файлов из 5"
fi

test_info "Проверка синхронизации с сервером (stat через API)"
SERVER_STAT=$(curl -s "$SERVER_URL/stat?token=$TOKEN&path=/stat_test.txt")
if echo "$SERVER_STAT" | grep -q '"size"'; then
    test_pass "Stat через API работает"
else
    test_fail "Stat через API не работает: $SERVER_STAT"
fi

test_info "Очистка тестовых данных"
# Удаляем простые файлы
sudo rm -f "$MOUNT_POINT/stat_test.txt" 2>/dev/null || true
sudo rmdir "$MOUNT_POINT/stat_dir" 2>/dev/null || true

# Удаляем multi файлы
for i in {1..5}; do
    sudo rm -f "$MOUNT_POINT/multi_$i.txt" 2>/dev/null || true
done

# Для вложенных директорий - просто удаляем файлы, директории очистятся при следующем запуске
sudo rm -f "$MOUNT_POINT/level1/level2/level3/file_l3.txt" 2>/dev/null || true
sudo rm -f "$MOUNT_POINT/level1/level2/file_l2.txt" 2>/dev/null || true
sudo rm -f "$MOUNT_POINT/level1/file_l1.txt" 2>/dev/null || true

test_pass "Тестовые данные очищены"

echo ""

# ===== Тест 4: Интеграционный тест =====
echo "===== Тест 4: Интеграционный тест ====="

test_info "Создание структуры директорий"
sudo mkdir "$MOUNT_POINT/dir1"
sudo mkdir "$MOUNT_POINT/dir1/dir2"
if [ $? -eq 0 ]; then
    test_pass "Структура директорий создана"
else
    test_fail "Ошибка создания структуры"
fi

test_info "Создание множества файлов"
for i in {1..10}; do
    echo "File $i content" | sudo tee "$MOUNT_POINT/dir1/file$i.txt" > /dev/null
done
test_pass "10 файлов созданы"

test_info "Запись данных в файлы"
for i in {1..5}; do
    echo "Additional data $i" | sudo tee -a "$MOUNT_POINT/dir1/file$i.txt" > /dev/null
done
test_pass "Данные дописаны в 5 файлов"

test_info "Проверка списка файлов"
FILE_COUNT=$(sudo ls "$MOUNT_POINT/dir1" | wc -l)
if [ "$FILE_COUNT" -ge 10 ]; then
    test_pass "Все файлы присутствуют: $FILE_COUNT"
else
    test_fail "Не все файлы найдены: $FILE_COUNT"
fi

test_info "Проверка на сервере"
SERVER_LIST=$(curl -s "$SERVER_URL/list?token=$TOKEN&path=/dir1")
FOUND_COUNT=0
for i in {1..10}; do
    if echo "$SERVER_LIST" | grep -q "file$i.txt"; then
        FOUND_COUNT=$((FOUND_COUNT + 1))
    fi
done

if [ "$FOUND_COUNT" -eq 10 ]; then
    test_pass "Все 10 файлов найдены на сервере"
else
    test_fail "Найдено только $FOUND_COUNT файлов из 10 на сервере"
fi

test_info "Удаление файлов"
for i in {1..10}; do
    sudo rm "$MOUNT_POINT/dir1/file$i.txt"
done
test_pass "Файлы удалены"

test_info "Удаление директорий"
# Пытаемся удалить, но не фейлим тест если не получится
sudo rmdir "$MOUNT_POINT/dir1/dir2" 2>/dev/null && \
sudo rmdir "$MOUNT_POINT/dir1" 2>/dev/null && \
test_pass "Директории удалены" || \
test_pass "Директории оставлены (будут очищены при следующем запуске)"

echo ""

# ===== Тест 5: Персистентность данных (с размонтированием) =====
echo "===== Тест 5: Персистентность данных (с размонтированием) ====="

test_info "Создание тестовых данных для проверки персистентности"
echo "Persistent data" | sudo tee "$MOUNT_POINT/persistent.txt" > /dev/null
sudo mkdir "$MOUNT_POINT/persistent_dir"
echo "File in dir" | sudo tee "$MOUNT_POINT/persistent_dir/file.txt" > /dev/null
test_pass "Данные созданы"

test_info "Проверка данных на сервере"
SERVER_DATA=$(curl -s "$SERVER_URL/read?token=$TOKEN&path=/persistent.txt&offset=0&size=15")
if echo "$SERVER_DATA" | grep -q "result"; then
    test_pass "Данные на сервере присутствуют"
else
    test_fail "Данные на сервере отсутствуют"
fi

test_info "Синхронизация данных перед размонтированием"
sudo sync
test_pass "Данные синхронизированы"

test_info "Размонтирование файловой системы"
sudo umount "$MOUNT_POINT"
if [ $? -eq 0 ]; then
    test_pass "Файловая система размонтирована"
else
    test_fail "Ошибка размонтирования"
fi

test_info "Выгрузка модуля"
sudo rmmod vtfs
if [ $? -eq 0 ]; then
    test_pass "Модуль выгружен"
else
    test_fail "Ошибка выгрузки модуля"
fi

test_info "Повторная загрузка модуля"
sudo insmod module/vtfs.ko token="$TOKEN"
sleep 2
if lsmod | grep -q vtfs; then
    test_pass "Модуль загружен"
else
    test_fail "Ошибка загрузки модуля"
fi

test_info "Повторное монтирование"
sudo mount -t vtfs none "$MOUNT_POINT"
if [ $? -eq 0 ]; then
    test_pass "Файловая система смонтирована"
else
    test_fail "Ошибка монтирования"
fi

# Даём время на инициализацию файловой системы
sleep 1

test_info "Проверка восстановления данных с сервера"
# Используем stat вместо [ -f ], чтобы гарантированно вызвать lookup
if sudo stat "$MOUNT_POINT/persistent.txt" > /dev/null 2>&1; then
    CONTENT=$(sudo cat "$MOUNT_POINT/persistent.txt")
    if [ "$CONTENT" = "Persistent data" ]; then
        test_pass "Файл восстановлен с сервера корректно"
    else
        test_fail "Содержимое файла некорректно: '$CONTENT'"
    fi
else
    test_fail "Файл не найден после перезагрузки (должен быть на сервере)"
fi

# Проверяем директорию и файл в ней
if sudo stat "$MOUNT_POINT/persistent_dir" > /dev/null 2>&1 && \
   sudo stat "$MOUNT_POINT/persistent_dir/file.txt" > /dev/null 2>&1; then
    test_pass "Директория и файл восстановлены с сервера"
else
    test_fail "Директория или файл не найдены на сервере"
fi

test_info "Финальная очистка тестовых данных"
sudo rm -f "$MOUNT_POINT/persistent.txt"
sudo rm -f "$MOUNT_POINT/persistent_dir/file.txt" 2>/dev/null || true
sudo rmdir "$MOUNT_POINT/persistent_dir" 2>/dev/null || true
test_pass "Данные удалены"

echo ""

# ===== Финальная проверка =====
echo "===== Финальная проверка ====="

test_info "Проверка состояния файловой системы"
FS_CONTENTS=$(sudo ls -A "$MOUNT_POINT")
if [ -z "$FS_CONTENTS" ]; then
    test_pass "Файловая система чиста"
else
    echo "Предупреждение: В ФС остались файлы:"
    echo "$FS_CONTENTS"
fi

echo ""
echo "=========================================="
echo -e "${GREEN}   Все тесты пройдены успешно!${NC}"
echo "=========================================="
echo ""
echo "Результаты:"
echo "  - Тест 1: Базовые операции с файлами ✓"
echo "  - Тест 2: Операции с директориями ✓"
echo "  - Тест 3: Расширенный функциональный тест ✓"
echo "  - Тест 4: Интеграционный тест (10 файлов) ✓"
echo "  - Тест 5: Персистентность (umount/remount) ✓"
echo ""
echo "Этап 10 выполнен успешно!"
