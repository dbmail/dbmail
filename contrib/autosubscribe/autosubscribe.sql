
---
--- set of triggers and functions to automatically subscribe 
--- all normal users to newly created public mailboxes
---


---
--- list all user/mailboxes combinations for mailboxes
--- owner by __public__
---
drop view if exists public_mailbox_users;
create view public_mailbox_users (user_id, mailbox_id) as
select u.user_idnr,b.mailbox_idnr from dbmail_mailboxes b
        right outer join dbmail_users u on b.owner_idnr!=u.user_idnr
where
        userid not in ('__@!internal_delivery_user!@__','anyone','__public__') and
        owner_idnr=(select user_idnr from dbmail_users where userid='__public__');


--- 
--- insert a subscription
--- emulates replace into 
---
drop function if exists resubscribe(integer, integer);
create function resubscribe(integer, integer) returns void as $$
begin
	raise info 'resubscribe mailbox user: %, mailbox:% ...', $1, $2;
	if not exists(select * from dbmail_subscription where user_id=$1 and mailbox_id=$2) then
		raise info 'insert into dbmail_subscription (user_id, mailbox_id) values (%,%)', $1, $2;
		insert into dbmail_subscription (user_id, mailbox_id) values ($1, $2);
	end if;
	return ;
end;
$$ language plpgsql;


---
--- insert acl
--- emulates replace into
---
drop function if exists autogrant(integer, integer);
create function autogrant(integer, integer) returns void as $$
begin
	if not exists(select * from dbmail_acl where user_id=$2 and mailbox_id=$1) then
		raise info 'grant all on mailbox: % to %', $1,$2;
		insert into dbmail_acl (
			user_id,mailbox_id,lookup_flag,read_flag,seen_flag,
			write_flag,insert_flag,post_flag,create_flag,
			delete_flag,deleted_flag,expunge_flag,administer_flag) values ($2,$1, 1,1,1,1,1,1,1,1,1,1,1);
	end if;
	return;
end;
$$ language plpgsql;


---
--- trigger function for subscribing all users to a public
--- mailboxes and granting the anyone user access to them
---
drop function if exists auto_subscriber() cascade;
create function auto_subscriber() returns trigger as $$
declare
	prow RECORD;
	anyoneid INTEGER;
begin
	anyoneid := user_idnr from dbmail_users where userid='anyone';
	for prow in select * from public_mailbox_users loop
		raise info 'resubscribe mailbox %,% ...', prow.user_id, prow.mailbox_id;
		execute 'select resubscribe(' || prow.user_id || ',' || prow.mailbox_id || ')';
		raise info 'grant public access on mailbox % ...', prow.mailbox_id;
		execute 'select autogrant(' || prow.mailbox_id || ',' || anyoneid ||')';
	end loop;
	return NEW;
end;
$$ language plpgsql;

---
--- hook up the trigger
---
create trigger auto_subscribe_trigger after insert 
	on dbmail_mailboxes
	for each row execute procedure auto_subscriber();


