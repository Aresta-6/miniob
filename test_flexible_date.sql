drop table date_table;
create table date_table(id int, d date);
insert into date_table values(1, '2020-1-01');
insert into date_table values(2, '2020-01-1');
insert into date_table values(3, '2020-1-1');
insert into date_table values(4, '2020-10-28');
select * from date_table;

