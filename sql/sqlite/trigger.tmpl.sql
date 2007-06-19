--
-- table_a.key_a references table_b(key_b) 
--   on update cascade 
--   on delete cascade
--
DROP TRIGGER IF EXISTS fk_insert_#TABLE_A#_#TABLE_B#_#KEY_B#;
CREATE TRIGGER fk_insert_#TABLE_A#_#TABLE_B#_#KEY_B#
	BEFORE INSERT ON dbmail_#TABLE_A#
	FOR EACH ROW BEGIN
		SELECT CASE 
			WHEN (new.#KEY_A# IS NOT NULL)
				AND ((SELECT #KEY_B# FROM dbmail_#TABLE_B# WHERE #KEY_B# = new.#KEY_A#) IS NULL)
			THEN RAISE (ABORT, 'insert on table "dbmail_#TABLE_A#" violates foreign key constraint "fk_insert_#TABLE_A#_#TABLE_B#_#KEY_B#"')
		END;
	END;

DROP TRIGGER IF EXISTS fk_update_#TABLE_A#_#TABLE_B#_#KEY_B#;
CREATE TRIGGER fk_update_#TABLE_A#_#TABLE_B#_#KEY_B#
	BEFORE UPDATE ON dbmail_#TABLE_A#
	FOR EACH ROW BEGIN
		SELECT CASE 
			WHEN (new.#KEY_A# IS NOT NULL)
				AND ((SELECT #KEY_B# FROM dbmail_#TABLE_B# WHERE #KEY_B# = new.#KEY_A#) IS NULL)
			THEN RAISE (ABORT, 'update on table "dbmail_#TABLE_A#" violates foreign key constraint "fk_update_#TABLE_A#_#TABLE_B#_#KEY_B#"')
		END;
	END;

DROP TRIGGER IF EXISTS fk_update2_#TABLE_A#_#TABLE_B#_#KEY_B#;
CREATE TRIGGER fk_update2_#TABLE_A#_#TABLE_B#_#KEY_B#
	AFTER UPDATE ON dbmail_#TABLE_B#
	FOR EACH ROW BEGIN
		UPDATE dbmail_#TABLE_A# SET #KEY_A# = new.#KEY_B# WHERE #KEY_A# = OLD.#KEY_B#;
	END;

DROP TRIGGER IF EXISTS fk_delete_#TABLE_A#_#TABLE_B#_#KEY_B#;
CREATE TRIGGER fk_delete_#TABLE_A#_#TABLE_B#_#KEY_B#
	BEFORE DELETE ON dbmail_#TABLE_B#
	FOR EACH ROW BEGIN
		DELETE FROM dbmail_#TABLE_A# WHERE #KEY_A# = OLD.#KEY_B#;
	END;

