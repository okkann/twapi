/*
 * Copyright (c) 2010, Ashok P. Nadkarni
 * All rights reserved.
 *
 * See the file LICENSE for license
 */

#include <twapi.h>

struct OptionDescriptor {
    const char *name;
    int         name_len;
    Tcl_Obj    *def_value;
    Tcl_Obj    *value;
    Tcl_Obj    *valid_values;
    int         type;
#define OPT_ANY    0
#define OPT_BOOL   1
#define OPT_INT    2
#define OPT_SWITCH 3
};

static Tcl_Obj *TwapiParseargsBadValue (const char *error_type,
                                    Tcl_Obj *value,
                                    const char *opt_name, int opt_name_len)
{
    Tcl_Obj *resultObj = Tcl_NewStringObj("", 0);
    Tcl_AppendStringsToObj(resultObj, error_type, " value '", Tcl_GetString(value),
                           "' specified for option '-", NULL);
    Tcl_AppendToObj(resultObj, opt_name, opt_name_len);
    Tcl_AppendToObj(resultObj, "'", 1);
    return resultObj;
}

static void TwapiParseargsUnknownOption(Tcl_Interp *interp, char *badopt, struct OptionDescriptor *opts, int nopts)
{
    int j;
    Tcl_DString ds;
    char *sep = "-";

    Tcl_DStringInit(&ds);
    Tcl_DStringAppend(&ds, "Invalid option '", -1);
    Tcl_DStringAppend(&ds, badopt, -1);
    Tcl_DStringAppend(&ds, "'. Must be one of ", -1);
    for (j = 0; j < nopts; ++j) {
        Tcl_DStringAppend(&ds, sep, -1);
        Tcl_DStringAppend(&ds, opts[j].name, opts[j].name_len);
        sep = ", -";
    }
    
    Tcl_DStringResult(interp, &ds);
    Tcl_DStringFree(&ds);
}


/*
 * Argument parsing command
 */
int Twapi_ParseargsObjCmd(
    ClientData dummy,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *CONST objv[])
{
    Tcl_Obj    *argvObj;
    int         argc, iarg;
    Tcl_Obj   **argv;
    int         nopts;
    Tcl_Obj   **optObjs;
    int         j, k;
    Tcl_WideInt wide;
    struct OptionDescriptor opts[128]; /* Max number of options */
    int         ignoreunknown = 0;
    int         nulldefault = 0;
    Tcl_Obj    *newargvObj = NULL;
    int         maxleftover = INT_MAX;
    Tcl_Obj    *namevalList = NULL;

    /* TBD - also set errorCode in case of errors */

    if (objc < 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "argvVar optlist ?-ignoreunknown? ?-nulldefault? ?-?");
        Tcl_SetObjErrorCode(interp, Twapi_MakeTwapiErrorCodeObj(TWAPI_BAD_ARG_COUNT));
        return TCL_ERROR;
    }

    for (j = 3 ; j < objc ; ++j) {
        char *s = Tcl_GetString(objv[j]);
        if (STREQ("-ignoreunknown", s)) {
            ignoreunknown = 1;
        }
        else if (STREQ("-nulldefault", s)) {
            nulldefault = 1;
        }
        else if (STREQ("-maxleftover", s)) {
            ++j;
            if (j == objc) {
                Tcl_SetResult(interp, "Missing value for -maxleftover", TCL_STATIC);
                goto invalid_args_error;
            }

            if (Tcl_GetIntFromObj(interp, objv[j], &maxleftover) != TCL_OK) {
                Tcl_SetResult(interp, "Non-integer value specified for -maxleftover", TCL_STATIC);
                goto invalid_args_error;
            }
        }
        else {
            Tcl_AppendResult(interp, "Extra argument or unknown option '",
                             Tcl_GetString(objv[j]), "'", NULL);
            goto invalid_args_error;
        }
    }

    /* Collect the arguments into an array */
    argvObj = Tcl_ObjGetVar2(interp, objv[1], NULL, TCL_LEAVE_ERR_MSG);
    if (argvObj == NULL)
        return TCL_ERROR;

    if (Tcl_ListObjGetElements(interp, argvObj, &argc, &argv) != TCL_OK)
        return TCL_ERROR;

    /* Now construct the option descriptors */
    if (Tcl_ListObjGetElements(interp, objv[2], &nopts, &optObjs) != TCL_OK)
        return TCL_ERROR;

    if (nopts > (sizeof(opts)/sizeof(opts[0]))) {
        return TwapiReturnTwapiError(interp,
                                     "Too many options specified.",
                                     TWAPI_INTERNAL_LIMIT);
    }

    for (k = 0; k < nopts ; ++k) {
        Tcl_Obj **elems;
        int       nelems;
        const char     *type;

        if (Tcl_ListObjGetElements(interp, optObjs[k],
                                    &nelems, &elems) != TCL_OK) {
            Tcl_AppendResult(interp, "Badly formed option descriptor: '",
                             Tcl_GetString(optObjs[k]), "'", NULL);
            Tcl_SetObjErrorCode(interp, Twapi_MakeTwapiErrorCodeObj(TWAPI_INVALID_ARGS));
            return TCL_ERROR;
        }

        opts[k].name = Tcl_GetStringFromObj(elems[0], &opts[k].name_len);
        opts[k].type = OPT_SWITCH;
        opts[k].def_value = NULL;
        opts[k].value = NULL;
        opts[k].valid_values = NULL;
        type = Tcl_UtfFindFirst(opts[k].name, '.');
        if (type) {
            opts[k].name_len = (int) (type - opts[k].name);
            ++type;          /* Point to type descriptor */
            if (STREQ(type, "int"))
                opts[k].type = OPT_INT;
            else if (STREQ(type, "arg"))
                opts[k].type = OPT_ANY;
            else if (STREQ(type, "bool"))
                opts[k].type = OPT_BOOL;
            else if (STREQ(type, "switch"))
                opts[k].type = OPT_SWITCH;
            else {
                Tcl_AppendResult(interp, "Invalid option type '",
                                 type, "'", NULL);
                Tcl_SetObjErrorCode(interp, Twapi_MakeTwapiErrorCodeObj(TWAPI_INVALID_ARGS));
                return TCL_ERROR;
            }
        }
        if (nelems > 1) {
            /* Squirrel away specified default */
            opts[k].def_value = elems[1];

            if (nelems > 2) {
                /* Value must be in the specified list */
                opts[k].valid_values = elems[2];
            }
        }
    }

    newargvObj = Tcl_NewListObj(0, NULL);
    namevalList = Tcl_NewListObj(0, NULL);

    /* OK, now go through the passed arguments */
    for (iarg = 0; iarg < argc; ++iarg) {
        int   argp_len;
        char *argp = Tcl_GetStringFromObj(argv[iarg], &argp_len);

        /* Non-option arg or a '-' or a "--" signals end of arguments */
        if ((*argp != '-') ||
            (argp[1] == 0) ||
            (argp[1] == '-' && argp[2] == 0))
            break;              /* No more options */

        /* Check against each option in turn */
        for (j = 0; j < nopts; ++j) {
            if (opts[j].name_len == (argp_len-1) &&
                ! Tcl_UtfNcmp(opts[j].name, argp+1, (argp_len-1))) {
                break;          /* Match ! */
            }
        }

        if (j < nopts) {
            /*
             *  Matches option j. Remember the option value.
             */
            if (opts[j].type == OPT_SWITCH) {
                opts[j].value = Tcl_NewBooleanObj(1);
            }
            else {
                if (iarg >= (argc-1)) {
                    /* No more args! */
                    Tcl_AppendResult(interp, "No value supplied for option '", argp, "'", NULL);
                    Tcl_SetObjErrorCode(interp, Twapi_MakeTwapiErrorCodeObj(TWAPI_INVALID_ARGS));
                    goto error_return;
                }
                opts[j].value = argv[iarg+1];
                ++iarg;            /* Move on to next arg in array */
            }
        }
        else {
            /* Does not match any option. */
            if (! ignoreunknown) {
                TwapiParseargsUnknownOption(interp, argp, opts, nopts);
                Tcl_SetObjErrorCode(interp, Twapi_MakeTwapiErrorCodeObj(TWAPI_INVALID_ARGS));
                goto error_return;
            }
            /*
             * We have to ignore this option. But we do not know if
             * it takes an argument value or not. So we check the next
             * argument. If it does not begin with a "-" assume it
             * is the value of the unknown option. Else it is the next option
             */
            Tcl_ListObjAppendElement(interp, newargvObj, argv[iarg]);
            if (iarg < (argc-1)) {
                argp = Tcl_GetString(argv[iarg+1]);
                if (*argp != '-') {
                    /* Assume this is the value for the option */
                    ++iarg;
                    Tcl_ListObjAppendElement(interp, newargvObj, argv[iarg]);
                }
            }
        }
    }

    /*
     * Now loop through the option definitions, collecting the option
     * values, using defaults for unspecified options
     */
    for (k = 0; k < nopts ; ++k) {
        Tcl_Obj **validObjs;
        int       nvalid;
        int       ivalid;

        /* Construct array of allowed values if specified.
         * Since we created the list ourselves, call cannot fail
         */
        if (opts[k].valid_values) {
            (void) Tcl_ListObjGetElements(interp, opts[k].valid_values,
                                          &nvalid, &validObjs);
        }

        if (opts[k].value == NULL) {
            /* Option not specified. Check for a default and use it */
            if (opts[k].type != OPT_SWITCH) {
                if (opts[k].def_value == NULL && !nulldefault)
                    continue;       /* No default, so skip */
                opts[k].value = opts[k].def_value;
            }
        }
        /* Do type checking if necessary */
        switch (opts[k].type) {
        case OPT_INT:
            /* If no explicit default, but have a -nulldefault switch,
             * (else we would have continued above), return 0
             */
            if (opts[k].value == NULL) {
                opts[k].value = Tcl_NewIntObj(0);
            }
            else if (Tcl_GetWideIntFromObj(interp, opts[k].value, &wide) == TCL_ERROR) {
                Tcl_SetObjResult(interp,
                                 TwapiParseargsBadValue("Non-integer",
                                                        opts[k].value,
                                                        opts[k].name, opts[k].name_len));
                goto error_return;
            }

            /* Check list of allowed values if specified */
            if (opts[k].valid_values) {
                for (ivalid = 0; ivalid < nvalid; ++ivalid) {
                    Tcl_WideInt valid_wide;
                    if (Tcl_GetWideIntFromObj(interp, validObjs[ivalid], &valid_wide) == TCL_ERROR) {
                        Tcl_SetObjResult(interp, TwapiParseargsBadValue(
                                             "Non-integer enumeration",
                                             opts[k].value,
                                             opts[k].name, opts[k].name_len));
                        goto error_return;
                    }
                    if (valid_wide == wide)
                        break;
                }
            }
            break;

        case OPT_ANY:
            /* If no explicit default, but have a -nulldefault switch,
             * (else we would have continued above), return ""
             */
            if (opts[k].value == NULL) {
                opts[k].value = Tcl_NewStringObj("",0);
            }

            /* Check list of allowed values if specified */
            if (opts[k].valid_values) {
                for (ivalid = 0; ivalid < nvalid; ++ivalid) {
                    if (!lstrcmpA(Tcl_GetString(validObjs[ivalid]),
                                  Tcl_GetString(opts[k].value)))
                        break;
                }
            }
            break;

        case OPT_SWITCH:
            /* Fallthru */
        case OPT_BOOL:
            if (opts[k].value == NULL)
                opts[k].value = Tcl_NewBooleanObj(0);
            else {
                if (Tcl_GetBooleanFromObj(interp, opts[k].value, &j) == TCL_ERROR) {
                    Tcl_SetObjResult(interp, TwapiParseargsBadValue("Non-boolean",
                                                            opts[k].value,
                                                            opts[k].name, opts[k].name_len));
                    goto error_return;
                }
                //Note: Can't just do a SetBoolean as object is shared
                //Need to allocate a new obj
                //Tcl_SetBooleanObj(opts[k].value, j); /* Set as 1 or 0 */
                opts[k].value = Tcl_NewBooleanObj(j);
            }
            ivalid = 0;         /* Valid values are ignored for booleans */
            break;

        default:
            break;
        }

        /* Check if validity checks succeeded */
        if (opts[k].valid_values) {
            if (ivalid == nvalid) {
                Tcl_Obj *invalidObj = TwapiParseargsBadValue("Invalid",
                                                             opts[k].value,
                                                             opts[k].name, opts[k].name_len);
                Tcl_AppendStringsToObj(invalidObj, ". Must be one of ", NULL);
                TwapiAppendObjArray(invalidObj, nvalid, validObjs, ", ");
                Tcl_SetObjResult(interp, invalidObj);
                goto error_return;
            }
        }

        /* Tack it on to result */
        Tcl_ListObjAppendElement(interp, namevalList,
                                 Tcl_NewStringObj(opts[k].name,
                                                  opts[k].name_len));
        Tcl_ListObjAppendElement(interp, namevalList, opts[k].value);
    }


    if (maxleftover < (argc - iarg)) {
        Tcl_Obj *tooManyErrorObj;
        Tcl_SetObjErrorCode(interp, Twapi_MakeTwapiErrorCodeObj(TWAPI_EXTRA_ARGS));
        tooManyErrorObj = STRING_LITERAL_OBJ("Command has extra arguments specified: ");
        TwapiAppendObjArray(tooManyErrorObj, argc-iarg,
                            &argv[iarg], " ");
        Tcl_SetObjResult(interp, tooManyErrorObj);
        goto error_return;
    }

    /* Tack on the remaining items in the argument list to new argv */
    while (iarg < argc) {
        Tcl_ListObjAppendElement(interp, newargvObj, argv[iarg]);
        ++iarg;
    }

    if (Tcl_ObjSetVar2(interp, objv[1], NULL, newargvObj, TCL_LEAVE_ERR_MSG)
        == NULL) {
        goto error_return;
    }

    Tcl_SetObjResult(interp, namevalList);

    return TCL_OK;

invalid_args_error:
    Tcl_SetObjErrorCode(interp, Twapi_MakeTwapiErrorCodeObj(TWAPI_INVALID_ARGS));

error_return:
    if (newargvObj)
        Tcl_DecrRefCount(newargvObj);
    if (namevalList)
        Tcl_DecrRefCount(namevalList);
    return TCL_ERROR;
}


