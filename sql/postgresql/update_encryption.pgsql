USE dbmail;

ALTER TABLE users ADD encryption_type VARCHAR(20);
ALTER TABLE users ALTER encryption_type SET DEFAULT '';

UPDATE users SET encryption_type = '';

