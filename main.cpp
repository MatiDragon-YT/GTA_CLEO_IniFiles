#include <mod/amlmod.h>
#include <mod/logger.h>

#include <fstream>
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <map>

#include "thirdparty/inipp.h"

#include "cleo.h"
cleo_ifs_t* cleo = nullptr;

#include "cleoaddon.h"
cleo_addon_ifs_t* cleoaddon = nullptr;

inline void CreateDirs(std::string root, const char* filename_with_dir)
{
    std::string sFileDir = std::string(filename_with_dir);
    size_t found = sFileDir.find_last_of("/");
    if(found < 0) return; // Is not in any directories

    sFileDir = sFileDir.substr(0, found);
    std::string baseroot = root;
    DIR* dir = opendir((root + sFileDir).c_str());
    if(!dir)
    {
        char only_file_path[128];
        snprintf(only_file_path, sizeof(only_file_path), "%s", sFileDir.c_str());

        char* pch = strtok(only_file_path, "/");
        if(pch)
        {
            while(pch != NULL)
            {
                baseroot += pch;
                baseroot += '/';

                mkdir(baseroot.c_str(), 0777);
                pch = strtok(NULL, "/");
            }
        }
        else
        {
            mkdir(baseroot.c_str(), 0777);
        }
    }
    else
    {
        closedir(dir);
    }
}

MYMOD(net.alexblade.rusjj.inifiles, CLEO4 IniFiles, 1.4, Alexander Blade & RusJJ & MatiDragon)
BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.cleolib, 2.0.1.6)
END_DEPLIST()

#define CLEO_RegisterOpcode(x, h) cleo->RegisterOpcode(x, h); cleo->RegisterOpcodeFunction(#h, h)
#define CLEO_Fn(h) void h (void *handle, uint32_t *ip, uint16_t opcode, const char *name)  

std::string sGameFilesRoot;
static char szConvertedValue[16];

// Mapa de sesiones de edición activas (clave = ruta completa del archivo)
std::map<std::string, inipp::Ini<char>> g_editSessions;

CLEO_Fn(START_INI_EDIT)
{
    char filename[128];
    cleoaddon->ReadString(handle, filename, sizeof(filename)); 
    filename[sizeof(filename)-1] = 0;
    
    // Normalizar barras invertidas
    for (int i = 0; filename[i]; ++i)
        if (filename[i] == '\\') filename[i] = '/';
    
    std::string fullPath = sGameFilesRoot + filename;
    CreateDirs(sGameFilesRoot, filename);   // Crea la ruta si no existe
    
    inipp::Ini<char> ini;
    std::ifstream is(fullPath);
    if (is.is_open())
    {
        ini.parse(is);
        is.close();
    }
    // Si el archivo no existía, la sesión empieza con un INI vacío
    
    g_editSessions[fullPath] = std::move(ini);
    cleoaddon->UpdateCompareFlag(handle, true);
}

CLEO_Fn(END_INI_EDIT)
{
    char filename[128];
    cleoaddon->ReadString(handle, filename, sizeof(filename)); 
    filename[sizeof(filename)-1] = 0;
    
    for (int i = 0; filename[i]; ++i)
        if (filename[i] == '\\') filename[i] = '/';
    
    std::string fullPath = sGameFilesRoot + filename;
    auto it = g_editSessions.find(fullPath);
    bool success = false;
    if (it != g_editSessions.end())
    {
        std::ofstream os(fullPath, std::ios::trunc);
        if (os.is_open())
        {
            it->second.generate(os);
            os.flush();
            os.close();
            success = true;
        }
        g_editSessions.erase(it);
    }
    cleoaddon->UpdateCompareFlag(handle, success);
}

CLEO_Fn(READ_INT_FROM_INI_FILE)
{
    inipp::Ini<char> ini;
    char filename[128], section[64], key[64];
    int result = 0, i = 0;
    cleoaddon->ReadString(handle, filename, sizeof(filename)); filename[sizeof(filename)-1] = 0;
    cleoaddon->ReadString(handle, section, sizeof(section)); section[sizeof(section)-1] = 0;
    cleoaddon->ReadString(handle, key, sizeof(key)); key[sizeof(key)-1] = 0;
    while(filename[i] != 0)
    {
        if(filename[i] == '\\') filename[i] = '/';
        ++i;
    }

    // Posible mejora: si hay sesión activa, leer de ella.
    std::string fullPath = sGameFilesRoot + filename;
    bool didReadValue = false;
    auto session = g_editSessions.find(fullPath);
    if (session != g_editSessions.end())
    {
        didReadValue = inipp::get_value(session->second.sections[section], key, result);
    }
    else
    {
        CreateDirs(sGameFilesRoot, filename);
        std::ifstream is(fullPath);
        if(is.is_open())
        {
            ini.parse(is);
            didReadValue = inipp::get_value(ini.sections[section], key, result);
            is.close();
        }
    }
    cleo->GetPointerToScriptVar(handle)->i = result;
    cleoaddon->UpdateCompareFlag(handle, didReadValue);
    ini.clear();
}

CLEO_Fn(WRITE_INT_TO_INI_FILE)
{
    char filename[128], section[64], key[64];
    int i = 0, value = cleo->ReadParam(handle)->i;
    cleoaddon->ReadString(handle, filename, sizeof(filename)); filename[sizeof(filename)-1] = 0;
    cleoaddon->ReadString(handle, section, sizeof(section)); section[sizeof(section)-1] = 0;
    cleoaddon->ReadString(handle, key, sizeof(key)); key[sizeof(key)-1] = 0;
    while(filename[i] != 0)
    {
        if(filename[i] == '\\') filename[i] = '/';
        ++i;
    }

    std::string fullPath = sGameFilesRoot + filename;
    
    // Si hay una sesión de edición activa, modificamos directamente en memoria
    auto session = g_editSessions.find(fullPath);
    if (session != g_editSessions.end())
    {
        session->second.sections[section][key] = std::to_string(value);
        cleoaddon->UpdateCompareFlag(handle, true);
        return;
    }
    
    // Comportamiento original: abrir, modificar, guardar
    inipp::Ini<char> ini;
    CreateDirs(sGameFilesRoot, filename);
    std::ifstream is(fullPath);
    if(is.is_open())
    {
        ini.parse(is);
        is.close();
    }
    else ini.clear();

    ini.sections[section][key] = std::to_string(value);

    std::ofstream os(fullPath, std::ios::trunc);
    if(os.is_open())
    {
        ini.generate(os);
        os.flush();
        os.close();
        cleoaddon->UpdateCompareFlag(handle, true);
    }
    else cleoaddon->UpdateCompareFlag(handle, false);
}

CLEO_Fn(READ_FLOAT_FROM_INI_FILE)
{
    inipp::Ini<char> ini;
    char filename[128], section[64], key[64];
    float result = 0.0f; int i = 0;
    cleoaddon->ReadString(handle, filename, sizeof(filename)); filename[sizeof(filename)-1] = 0;
    cleoaddon->ReadString(handle, section, sizeof(section)); section[sizeof(section)-1] = 0;
    cleoaddon->ReadString(handle, key, sizeof(key)); key[sizeof(key)-1] = 0;
    while(filename[i] != 0)
    {
        if(filename[i] == '\\') filename[i] = '/';
        ++i;
    }

    std::string fullPath = sGameFilesRoot + filename;
    bool didReadValue = false;
    auto session = g_editSessions.find(fullPath);
    if (session != g_editSessions.end())
    {
        didReadValue = inipp::get_value(session->second.sections[section], key, result);
    }
    else
    {
        CreateDirs(sGameFilesRoot, filename);
        std::ifstream is(fullPath);
        if(is.is_open())
        {
            ini.parse(is);
            didReadValue = inipp::get_value(ini.sections[section], key, result);
            is.close();
        }
    }
    cleo->GetPointerToScriptVar(handle)->f = result;
    cleoaddon->UpdateCompareFlag(handle, didReadValue);
    ini.clear();
}

CLEO_Fn(WRITE_FLOAT_TO_INI_FILE)
{
    char filename[128], section[64], key[64];
    int i = 0; float value = cleo->ReadParam(handle)->f;
    cleoaddon->ReadString(handle, filename, sizeof(filename)); filename[sizeof(filename)-1] = 0;
    cleoaddon->ReadString(handle, section, sizeof(section)); section[sizeof(section)-1] = 0;
    cleoaddon->ReadString(handle, key, sizeof(key)); key[sizeof(key)-1] = 0;
    while(filename[i] != 0)
    {
        if(filename[i] == '\\') filename[i] = '/';
        ++i;
    }

    std::string fullPath = sGameFilesRoot + filename;
    
    auto session = g_editSessions.find(fullPath);
    if (session != g_editSessions.end())
    {
        session->second.sections[section][key] = std::to_string(value);
        cleoaddon->UpdateCompareFlag(handle, true);
        return;
    }
    
    inipp::Ini<char> ini;
    CreateDirs(sGameFilesRoot, filename);
    std::ifstream is(fullPath);
    if(is.is_open())
    {
        ini.parse(is);
        is.close();
    }
    else ini.clear();

    ini.sections[section][key] = std::to_string(value);

    std::ofstream os(fullPath, std::ios::trunc);
    if(os.is_open())
    {
        ini.generate(os);
        os.flush();
        os.close();
        cleoaddon->UpdateCompareFlag(handle, true);
    }
    else cleoaddon->UpdateCompareFlag(handle, false);
}

char valRes[100];
CLEO_Fn(READ_STRING_FROM_INI_FILE)
{
    inipp::Ini<char> ini;
    char filename[128], section[64], key[64];
    valRes[0] = 0; int i = 0;
    cleoaddon->ReadString(handle, filename, sizeof(filename)); filename[sizeof(filename)-1] = 0;
    cleoaddon->ReadString(handle, section, sizeof(section)); section[sizeof(section)-1] = 0;
    cleoaddon->ReadString(handle, key, sizeof(key)); key[sizeof(key)-1] = 0;
    while(filename[i] != 0)
    {
        if(filename[i] == '\\') filename[i] = '/';
        ++i;
    }

    std::string fullPath = sGameFilesRoot + filename;
    bool didReadValue = false;
    auto session = g_editSessions.find(fullPath);
    if (session != g_editSessions.end())
    {
        const std::string& val = session->second.sections[section][key];
        snprintf(valRes, sizeof(valRes), "%s", val.c_str());
        didReadValue = (valRes[0] != 0);
    }
    else
    {
        CreateDirs(sGameFilesRoot, filename);
        std::ifstream is(fullPath);
        if(is.is_open())
        {
            ini.parse(is);
            snprintf(valRes, sizeof(valRes), "%s", ini.sections[section][key].c_str());
            didReadValue = (valRes[0] != 0);
            is.close();
        }
    }
    cleoaddon->WriteString(handle, valRes);
    cleoaddon->UpdateCompareFlag(handle, didReadValue);
    ini.clear();
}

CLEO_Fn(WRITE_STRING_TO_INI_FILE)
{
    char filename[128], section[64], key[64], value[128] {0};
    int i = 0;
    cleoaddon->ReadString(handle, value, sizeof(value));
    cleoaddon->ReadString(handle, filename, sizeof(filename)); filename[sizeof(filename)-1] = 0;
    cleoaddon->ReadString(handle, section, sizeof(section)); section[sizeof(section)-1] = 0;
    cleoaddon->ReadString(handle, key, sizeof(key)); key[sizeof(key)-1] = 0;
    while(filename[i] != 0)
    {
        if(filename[i] == '\\') filename[i] = '/';
        ++i;
    }

    std::string fullPath = sGameFilesRoot + filename;
    
    auto session = g_editSessions.find(fullPath);
    if (session != g_editSessions.end())
    {
        session->second.sections[section][key] = value;
        cleoaddon->UpdateCompareFlag(handle, true);
        return;
    }
    
    inipp::Ini<char> ini;
    CreateDirs(sGameFilesRoot, filename);
    std::ifstream is(fullPath);
    if(is.is_open())
    {
        ini.parse(is);
        is.close();
    }
    else ini.clear();

    ini.sections[section][key] = value;

    std::ofstream os(fullPath, std::ios::trunc);
    if(os.is_open())
    {
        ini.generate(os);
        os.flush();
        os.close();
        cleoaddon->UpdateCompareFlag(handle, true);
    }
    else cleoaddon->UpdateCompareFlag(handle, false);
}

extern "C" void OnModLoad()
{
    logger->SetTag("[CLEO] IniFiles");
    logger->Info("Starting...");
    if(!(cleo = (cleo_ifs_t*)GetInterface("CLEO")))
    {
        logger->Error("Cannot load a mod: CLEO's interface is unknown!");
        return;
    }
    if(!(cleoaddon = (cleo_addon_ifs_t*)GetInterface("CLEOAddon")))
    {
        logger->Error("Cannot load a mod: CLEO's Addon interface is unknown!");
        return;
    }
    
    sGameFilesRoot = aml->GetAndroidDataPath();
    sGameFilesRoot += "/";

    CLEO_RegisterOpcode(0x0AF0, READ_INT_FROM_INI_FILE);    // 0AF0=4,%4d% = read_int_from_ini_file %1s% section %2s% key %3s%
    CLEO_RegisterOpcode(0x0AF1, WRITE_INT_TO_INI_FILE);    // 0AF1=4,write_int %1d% to_ini_file %2s% section %3s% key %4s%
    CLEO_RegisterOpcode(0x0AF2, READ_FLOAT_FROM_INI_FILE);  // 0AF2=4,%4d% = read_float_from_ini_file %1s% section %2s% key %3s%
    CLEO_RegisterOpcode(0x0AF3, WRITE_FLOAT_TO_INI_FILE);   // 0AF3=4,write_float %1d% to_ini_file %2s% section %3s% key %4s%
    CLEO_RegisterOpcode(0x0AF4, READ_STRING_FROM_INI_FILE); // 0AF4=4,%4d% = read_string_from_ini_file %1s% section %2s% key %3s%
    CLEO_RegisterOpcode(0x0AF5, WRITE_STRING_TO_INI_FILE);  // 0AF5=4,write_string %1s% to_ini_file %2s% section %3s% key %4s%

    CLEO_RegisterOpcode(0x0AF6, START_INI_EDIT);            // 0AF6=1,start_ini_edit %1s%
    CLEO_RegisterOpcode(0x0AF7, END_INI_EDIT);              // 0AF7=1,end_ini_edit %1s%
}
