# Dossier personnel de la personne connectée (les scripts d'installation tournent
# en administrateur : $HOME n'est pas le sien).
CONSOLE_USER="$(stat -f%Su /dev/console 2>/dev/null || true)"
USER_HOME=""
if [ -n "$CONSOLE_USER" ] && [ "$CONSOLE_USER" != "root" ]; then
  USER_HOME="$(dscl . -read "/Users/$CONSOLE_USER" NFSHomeDirectory 2>/dev/null | awk '{print $2}')"
fi
