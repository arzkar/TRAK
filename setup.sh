#!/bin/bash

# TRAK Setup Script
set -e

echo "🚀 Starting TRAK Backend Setup..."

# 1. Environment Files
if [ ! -f trak-backend/.env ]; then
  echo "📄 Creating trak-backend/.env from .env.example..."
  cp trak-backend/.env.example trak-backend/.env
else
  echo "✅ trak-backend/.env already exists."
fi

# 1b. Firmware Secrets
if [ ! -f trak-firmware/main/secrets.h ]; then
  echo "📄 Creating trak-firmware/main/secrets.h from secrets.h.example..."
  cp trak-firmware/main/secrets.h.example trak-firmware/main/secrets.h
else
  echo "✅ trak-firmware/main/secrets.h already exists."
fi

# 2. Docker Containers
echo "🐳 Starting Docker containers..."
cd trak-backend
docker compose up -d

# 3. Backend Dependencies
echo "📦 Installing backend dependencies..."
bun install

# 4. Database Schema
echo "🗄️ Pushing database schema..."
bun db:push

# 5. TimescaleDB Initialization
echo "📈 Initializing TimescaleDB hypertables..."
# Give PG a second to breathe
sleep 5

# Extract variables from .env (robust parsing)
DB_USER=$(grep "^DB_USER=" .env | cut -d '=' -f2 | tr -d '\r' | xargs)
DB_NAME=$(grep "^DB_NAME=" .env | cut -d '=' -f2 | tr -d '\r' | xargs)

if [ -z "$DB_USER" ] || [ -z "$DB_NAME" ]; then
  echo "⚠️  Warning: DB_USER or DB_NAME not found in .env, falling back to 'trak'"
  DB_USER=${DB_USER:-trak}
  DB_NAME=${DB_NAME:-trak}
fi

echo "Connecting as $DB_USER to database $DB_NAME..."
docker exec -i trak-db psql -U "$DB_USER" -d "$DB_NAME" < scripts/init-timescale.sql

echo "✨ Setup complete!"
echo "👉 Run 'bun dev' in trak-backend to start the server."
