// Retour arriere automatique apres une mise a jour.
// En renvoyant true, on empeche Arduino de valider d'office la nouvelle version :
// c'est le programme qui se valide lui-meme apres quelques secondes de bon fonctionnement
// (esp_ota_mark_app_valid_cancel_rollback dans le .ino). S'il plante ou se bloque avant,
// le demarreur revient tout seul a la version precedente.
// (fichier .cpp separe : le generateur de prototypes Arduino gere mal extern "C" dans un .ino)
extern "C" bool verifyRollbackLater() { return true; }
