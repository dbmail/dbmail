# will change the owner_id field into client_id
# aliases belong to an owner or just to client_id 0 (default)

use dbmail;

ALTER TABLE aliases CHANGE owner_id client_id INTEGER;
