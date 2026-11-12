#!/system/bin/sh
# HoloDeck - Daemon Lector Serial USB para Android (Magisk Root)
# Lee la ESP32 conectada por cable USB OTG y reenvía comandos a la interfaz web
sleep 3

while true; do
  PORT=""
  for dev in /dev/ttyACM* /dev/ttyUSB*; do
    if [ -c "$dev" ]; then
      PORT="$dev"
      break
    fi
  done

  if [ -n "$PORT" ]; then
    chmod 666 "$PORT" 2>/dev/null
    stty -F "$PORT" 115200 raw -echo 2>/dev/null

    while IFS= read -r line || [ -n "$line" ]; do
      cmd=$(echo "$line" | tr -d '\r\n ')
      [ -z "$cmd" ] && continue

      case "$cmd" in
        "ENC:UP")
          input keyevent 131
          ;;
        "ENC:DOWN")
          input keyevent 132
          ;;
        "BTN:CLICK")
          input keyevent 133
          ;;
        "BTN:HOLD")
          input keyevent 134
          ;;
        "TAG:01")
          input keyevent 135
          ;;
        "TAG:02")
          input keyevent 136
          ;;
        "TAG:03")
          input keyevent 137
          ;;
        "TAG:EJECT")
          input keyevent 138
          ;;
      esac
    done < "$PORT"
  fi
  sleep 1
done
