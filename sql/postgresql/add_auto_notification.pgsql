
CREATE SEQUENCE auto_notification_seq;
CREATE TABLE auto_notifications (
   auto_notify_idnr INT8 DEFAULT nextval('auto_notification_seq'),
   user_idnr INT8 DEFAULT '0' NOT NULL,
   notify_address VARCHAR(100)
);

