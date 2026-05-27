class CfgPatches {
    class BrigadaZ_Transport {
        units[] = { "BZBusStopSign" };
        weapons[] = {};
        requiredVersion = 0.1;
        requiredAddons[] = {
            "DZ_Data",
            "DayZExpansion_AI_Scripts",
            "DayZExpansion_Vehicles_Scripts"
        };
    };
};

class CfgMods {
    class BrigadaZ_Transport {
        dir = "BrigadaZ_Transport";
        picture = "";
        action = "";
        hideName = 1;
        hidePicture = 1;
        name = "BZ_Transport (Bus eAI Driving)";
        credits = "Sonom4n, Hiperhipo10";
        author = "Sonom4n, Hiperhipo10";
        authorID = "0";
        version = "1.0.0";
        extra = 0;
        type = "mod";

        dependencies[] = { "Game", "World", "Mission" };

        class defs {
            class gameScriptModule {
                value = "";
                files[] = { "BrigadaZ_Transport/scripts/3_Game" };
            };
            class worldScriptModule {
                value = "";
                files[] = { "BrigadaZ_Transport/scripts/4_World" };
            };
            class missionScriptModule {
                value = "";
                files[] = { "BrigadaZ_Transport/scripts/5_Mission" };
            };
        };
    };
};

class CfgVehicles {
    class HouseNoDestruct;
    class BZBusStopSign : HouseNoDestruct {
        scope = 2;
        model = "\DZ\structures_bliss\signs\sign_citydir_A.p3d";
        vehicleClass = "props";
        hiddenSelections[] = {};
        hiddenSelectionsTextures[] = {};
    };
};
