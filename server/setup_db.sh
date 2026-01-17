#!/bin/bash

# Скрипт для настройки PostgreSQL базы данных для VTFS сервера

set -e

echo "Setting up PostgreSQL database for VTFS server..."

# Проверяем, установлен ли PostgreSQL
if ! command -v psql &> /dev/null; then
    echo "PostgreSQL is not installed. Please install it first:"
    echo "  Ubuntu/Debian: sudo apt-get install postgresql postgresql-contrib"
    echo "  macOS: brew install postgresql"
    exit 1
fi

# Параметры подключения
DB_NAME="vtfs_db"
DB_USER="vtfs_user"
DB_PASSWORD="vtfs_password"

# Проверяем, запущен ли PostgreSQL
if ! pg_isready &> /dev/null; then
    echo "PostgreSQL is not running. Please start it first:"
    echo "  Ubuntu/Debian: sudo systemctl start postgresql"
    echo "  macOS: brew services start postgresql"
    exit 1
fi

# Создаём базу данных и пользователя
echo "Creating database and user..."

sudo -u postgres psql <<EOF
-- Создаём пользователя
CREATE USER $DB_USER WITH PASSWORD '$DB_PASSWORD';

-- Создаём базу данных
CREATE DATABASE $DB_NAME OWNER $DB_USER;

-- Предоставляем права
GRANT ALL PRIVILEGES ON DATABASE $DB_NAME TO $DB_USER;

\q
EOF

echo "Database setup complete!"
echo ""
echo "Database: $DB_NAME"
echo "User: $DB_USER"
echo "Password: $DB_PASSWORD"
echo ""
echo "You can now start the Spring Boot server."
