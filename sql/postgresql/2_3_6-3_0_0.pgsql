
BEGIN;
DROP INDEX IF EXISTS dbmail_envelope_1;
DROP INDEX IF EXISTS dbmail_envelope_2;
CREATE UNIQUE INDEX dbmail_envelope_1 ON dbmail_envelope(physmessage_id);
CREATE UNIQUE INDEX dbmail_envelope_2 ON dbmail_envelope(physmessage_id, id);
COMMIT;

