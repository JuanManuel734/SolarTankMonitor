unit uPrincipal;

{
  Monitor del calentador solar - App multiplataforma (FireMonkey)
  Plataformas: Windows (Win32/Win64) y Android.

  Se comunica con el ESP32 (firmware v2) por HTTP en la red local:
    /data       lecturas (cada petición mantiene despierto al ESP32)
    /status     estado del equipo
    /wake?min=N mantenerlo despierto N minutos
    /sleep      dormirlo ahora
    /calibrate?point=empty|full
}

interface

uses
  System.SysUtils, System.Types, System.UITypes, System.Classes,
  System.Variants, System.Math, System.JSON, System.Threading,
  System.IOUtils, System.IniFiles, System.Generics.Collections,
  System.Net.HttpClient, System.Net.URLClient,
  FMX.Types, FMX.Controls, FMX.Forms, FMX.Graphics, FMX.Dialogs,
  FMX.DialogService, FMX.StdCtrls, FMX.Objects, FMX.Layouts, FMX.Edit,
  FMX.ListBox, FMX.Controls.Presentation, FMX.Platform;

type
  TLectura = record
    Hora: TDateTime;
    Adc: Integer;
    Voltaje: Double;
    Nivel: Double;
    NivelCrudo: Double;
    Temp: Double;
    TempValida: Boolean;
    Estado: string;
  end;

  TRespuestaProc = reference to procedure(Ok: Boolean; Codigo: Integer;
    const Cuerpo: string);

  TfrmPrincipal = class(TForm)
    tbSuperior: TToolBar;
    rctBarra: TRectangle;
    lblTitulo: TLabel;
    lytEstado: TLayout;
    crcEstado: TCircle;
    lblEstado: TLabel;
    vsbPrincipal: TVertScrollBox;
    rctConexion: TRectangle;
    lblConexion: TLabel;
    lytHost: TLayout;
    edtHost: TEdit;
    btnConectar: TButton;
    lytEnergia: TLayout;
    btnDespertar: TButton;
    btnDormir: TButton;
    lblEnergia: TLabel;
    rctTemperatura: TRectangle;
    lblTempTitulo: TLabel;
    lblTemp: TLabel;
    lytEstadoTemp: TLayout;
    rctEstadoTemp: TRectangle;
    lblEstadoTemp: TLabel;
    rctTermoFondo: TRectangle;
    rctTermoRelleno: TRectangle;
    lytEscala: TLayout;
    lblEscala0: TLabel;
    lblEscala100: TLabel;
    lytStats: TLayout;
    lblMin: TLabel;
    lblMax: TLabel;
    lblProm: TLabel;
    pbxGrafica: TPaintBox;
    rctNivel: TRectangle;
    lblNivelTitulo: TLabel;
    lytNivel: TLayout;
    rctTanque: TRectangle;
    rctAgua: TRectangle;
    lytNivelDatos: TLayout;
    lblNivel: TLabel;
    lblNivelCrudo: TLabel;
    lblAdc: TLabel;
    lblVoltaje: TLabel;
    rctCalibracion: TRectangle;
    lblCalTitulo: TLabel;
    lytCal: TLayout;
    btnCalVacio: TButton;
    btnCalLleno: TButton;
    lblCal: TLabel;
    rctHistorial: TRectangle;
    lblHistTitulo: TLabel;
    lbHistorial: TListBox;
    lytHistBotones: TLayout;
    btnExportar: TButton;
    btnLimpiar: TButton;
    tmrSondeo: TTimer;
    procedure FormCreate(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure FormResize(Sender: TObject);
    procedure btnConectarClick(Sender: TObject);
    procedure btnDespertarClick(Sender: TObject);
    procedure btnDormirClick(Sender: TObject);
    procedure btnCalVacioClick(Sender: TObject);
    procedure btnCalLlenoClick(Sender: TObject);
    procedure btnExportarClick(Sender: TObject);
    procedure btnLimpiarClick(Sender: TObject);
    procedure tmrSondeoTimer(Sender: TObject);
    procedure pbxGraficaPaint(Sender: TObject; Canvas: TCanvas);
  private
    FLecturas: TList<TLectura>;
    FOcupado: Boolean;
    FMonitoreando: Boolean;
    FReanudarAlVolver: Boolean;
    FFallosSeguidos: Integer;
    FTempMin, FTempMax, FTempSuma: Double;
    FTempCuenta: Integer;
    FPuntoCal: string;
    function UrlBase: string;
    function RutaConfig: string;
    procedure CargarConfig;
    procedure GuardarConfig;
    procedure EstiloTexto(L: TLabel; Tamano: Single; Negrita: Boolean;
      Color: TAlphaColor; Alin: TTextAlign = TTextAlign.Leading);
    procedure Peticion(const Ruta: string; Callback: TRespuestaProc);
    procedure EntregarResultado(Callback: TRespuestaProc; Ok: Boolean;
      Codigo: Integer; Cuerpo: string);
    procedure EnviarCalibracion;
    procedure SetMonitoreo(Activo: Boolean);
    procedure MostrarConexion(const Texto: string; Color: TAlphaColor);
    procedure ProcesarDatos(const Cuerpo: string);
    procedure ProcesarStatus(const Cuerpo: string);
    procedure MostrarTemperatura(const L: TLectura);
    procedure MostrarNivel(const L: TLectura);
    procedure AgregarHistorial(const L: TLectura);
    procedure ConsultarStatus;
    procedure Calibrar(const Punto, Descripcion: string);
    procedure Aviso(const Msg: string);
    function EventoApp(AAppEvent: TApplicationEvent;
      AContext: TObject): Boolean;
  end;

var
  frmPrincipal: TfrmPrincipal;

implementation

{$R *.fmx}

const
  MAX_LECTURAS = 2000;     // en memoria para exportar
  MAX_HISTORIAL = 100;     // filas visibles
  MAX_GRAFICA = 100;       // puntos en la gráfica

  COLOR_OK = $FF43A047;
  COLOR_REPOSO = $FFFFA000;
  COLOR_ERROR = $FFE53935;
  COLOR_GRIS = $FF9E9E9E;
  COLOR_TEMP = $FFE65100;
  COLOR_NIVEL = $FF1E88E5;

{ ---------- utilidades JSON ---------- }

function JsonNumero(Obj: TJSONObject; const Nombre: string;
  out Valor: Double): Boolean;
var
  V: TJSONValue;
begin
  Result := False;
  Valor := 0;
  V := Obj.GetValue(Nombre);
  if (V <> nil) and (V is TJSONNumber) then
  begin
    Valor := TJSONNumber(V).AsDouble;
    Result := True;
  end;
end;

function JsonTexto(Obj: TJSONObject; const Nombre: string): string;
var
  V: TJSONValue;
begin
  V := Obj.GetValue(Nombre);
  if (V <> nil) and not (V is TJSONNull) then
    Result := V.Value
  else
    Result := '';
end;

function EstadoTemperatura(T: Double; out Color: TAlphaColor): string;
begin
  if T < 25 then
  begin
    Color := $FF1E88E5; Result := 'Fr'#237'a';
  end
  else if T < 40 then
  begin
    Color := $FF43A047; Result := 'Tibia';
  end
  else if T < 60 then
  begin
    Color := $FFFB8C00; Result := 'Caliente';
  end
  else
  begin
    Color := $FFE53935; Result := 'Muy caliente';
  end;
end;

{ ---------- formulario ---------- }

procedure TfrmPrincipal.FormCreate(Sender: TObject);
var
  Svc: IFMXApplicationEventService;
begin
  FLecturas := TList<TLectura>.Create;
  FTempCuenta := 0;

  EstiloTexto(lblTitulo, 18, True, TAlphaColors.White);
  EstiloTexto(lblEstado, 12, False, TAlphaColors.White);
  EstiloTexto(lblConexion, 15, True, $FF555555);
  EstiloTexto(lblTempTitulo, 15, True, $FF555555);
  EstiloTexto(lblNivelTitulo, 15, True, $FF555555);
  EstiloTexto(lblCalTitulo, 15, True, $FF555555);
  EstiloTexto(lblHistTitulo, 15, True, $FF555555);
  EstiloTexto(lblEnergia, 12, False, $FF555555);
  EstiloTexto(lblTemp, 44, True, COLOR_GRIS, TTextAlign.Center);
  EstiloTexto(lblEstadoTemp, 13, True, TAlphaColors.White, TTextAlign.Center);
  EstiloTexto(lblEscala0, 11, False, $FF888888);
  EstiloTexto(lblEscala100, 11, False, $FF888888, TTextAlign.Trailing);
  EstiloTexto(lblMin, 13, False, $FF333333, TTextAlign.Center);
  EstiloTexto(lblProm, 13, False, $FF333333, TTextAlign.Center);
  EstiloTexto(lblMax, 13, False, $FF333333, TTextAlign.Center);
  EstiloTexto(lblNivel, 40, True, COLOR_NIVEL);
  EstiloTexto(lblNivelCrudo, 13, False, $FF555555);
  EstiloTexto(lblAdc, 13, False, $FF555555);
  EstiloTexto(lblVoltaje, 13, False, $FF555555);
  EstiloTexto(lblCal, 12, False, $FF555555);

  CargarConfig;
  MostrarConexion('Sin conexi'#243'n', COLOR_GRIS);

  // En Android: pausar el sondeo cuando la app pasa a segundo plano,
  // así el ESP32 puede entrar en reposo.
  if TPlatformServices.Current.SupportsPlatformService(
       IFMXApplicationEventService, Svc) then
    Svc.SetApplicationEventHandler(EventoApp);
end;

procedure TfrmPrincipal.FormDestroy(Sender: TObject);
begin
  tmrSondeo.Enabled := False;
  GuardarConfig;
  FLecturas.Free;
end;

procedure TfrmPrincipal.FormResize(Sender: TObject);
var
  Mitad: Single;
begin
  // Dos botones por fila, del mismo ancho
  Mitad := (lytEnergia.Width - 8) / 2;
  btnDespertar.Width := Mitad;
  btnCalVacio.Width := Mitad;
  btnExportar.Width := Mitad;
  pbxGrafica.Repaint;
end;

procedure TfrmPrincipal.EstiloTexto(L: TLabel; Tamano: Single;
  Negrita: Boolean; Color: TAlphaColor; Alin: TTextAlign);
begin
  L.StyledSettings := [];
  L.TextSettings.Font.Size := Tamano;
  if Negrita then
    L.TextSettings.Font.Style := [TFontStyle.fsBold]
  else
    L.TextSettings.Font.Style := [];
  L.TextSettings.FontColor := Color;
  L.TextSettings.HorzAlign := Alin;
  L.TextSettings.VertAlign := TTextAlign.Center;
  L.TextSettings.WordWrap := True;
end;

function TfrmPrincipal.EventoApp(AAppEvent: TApplicationEvent;
  AContext: TObject): Boolean;
begin
  Result := True;
  case AAppEvent of
    TApplicationEvent.EnteredBackground:
      begin
        FReanudarAlVolver := FMonitoreando;
        if FMonitoreando then
          SetMonitoreo(False);
      end;
    TApplicationEvent.BecameActive:
      if FReanudarAlVolver then
      begin
        FReanudarAlVolver := False;
        SetMonitoreo(True);
      end;
  end;
end;

{ ---------- configuración ---------- }

function TfrmPrincipal.RutaConfig: string;
begin
  Result := System.IOUtils.TPath.Combine(System.IOUtils.TPath.GetDocumentsPath, 'monitor_calentador.ini');
end;

procedure TfrmPrincipal.CargarConfig;
var
  Ini: TMemIniFile;
begin
  try
    Ini := TMemIniFile.Create(RutaConfig);
    try
      edtHost.Text := Ini.ReadString('ESP32', 'Host', '');
    finally
      Ini.Free;
    end;
  except
    edtHost.Text := '';
  end;
end;

procedure TfrmPrincipal.GuardarConfig;
var
  Ini: TMemIniFile;
begin
  try
    Ini := TMemIniFile.Create(RutaConfig);
    try
      Ini.WriteString('ESP32', 'Host', Trim(edtHost.Text));
      Ini.UpdateFile;
    finally
      Ini.Free;
    end;
  except
    // no es crítico
  end;
end;

function TfrmPrincipal.UrlBase: string;
begin
  Result := Trim(edtHost.Text);
  if Result = '' then
    Exit;
  if not (Result.StartsWith('http://', True) or
          Result.StartsWith('https://', True)) then
    Result := 'http://' + Result;
  while Result.EndsWith('/') do
    Result := Result.Substring(0, Result.Length - 1);
end;

{ ---------- comunicación ---------- }

procedure TfrmPrincipal.Peticion(const Ruta: string; Callback: TRespuestaProc);
var
  Url: string;
begin
  Url := UrlBase;
  if Url = '' then
  begin
    Aviso('Escribe la IP del ESP32 (se muestra en el monitor serie).');
    Exit;
  end;
  Url := Url + Ruta;

  // La petición se hace en un hilo para no congelar la interfaz.
  TTask.Run(
    procedure
    var
      Cliente: THTTPClient;
      Resp: IHTTPResponse;
      Ok: Boolean;
      Codigo: Integer;
      Cuerpo: string;
    begin
      Ok := False;
      Codigo := 0;
      Cuerpo := '';
      Cliente := THTTPClient.Create;
      try
        try
          // Al despertar de reposo el ESP32 tarda un poco más en responder
          Cliente.ConnectionTimeout := 4000;
          Cliente.ResponseTimeout := 5000;
          Resp := Cliente.Get(Url);
          Codigo := Resp.StatusCode;
          Cuerpo := Resp.ContentAsString(TEncoding.UTF8);
          Ok := (Codigo >= 200) and (Codigo < 300);
        except
          on E: Exception do
            Cuerpo := E.Message;
        end;
      finally
        Cliente.Free;
      end;

      EntregarResultado(Callback, Ok, Codigo, Cuerpo);
    end);
end;

procedure TfrmPrincipal.EntregarResultado(Callback: TRespuestaProc;
  Ok: Boolean; Codigo: Integer; Cuerpo: string);
begin
  // Regresa el resultado al hilo principal (la interfaz sólo se toca ahí)
  TThread.Queue(nil,
    procedure
    begin
      Callback(Ok, Codigo, Cuerpo);
    end);
end;

procedure TfrmPrincipal.MostrarConexion(const Texto: string; Color: TAlphaColor);
begin
  lblEstado.Text := Texto;
  crcEstado.Fill.Color := Color;
end;

procedure TfrmPrincipal.SetMonitoreo(Activo: Boolean);
begin
  FMonitoreando := Activo;
  tmrSondeo.Enabled := Activo;
  if Activo then
  begin
    btnConectar.Text := 'Detener';
    FFallosSeguidos := 0;
    tmrSondeoTimer(nil);
  end
  else
  begin
    btnConectar.Text := 'Monitorear';
    MostrarConexion('En pausa', COLOR_GRIS);
  end;
end;

procedure TfrmPrincipal.btnConectarClick(Sender: TObject);
begin
  if FMonitoreando then
    SetMonitoreo(False)
  else
  begin
    if UrlBase = '' then
    begin
      Aviso('Escribe la IP del ESP32 (se muestra en el monitor serie).');
      Exit;
    end;
    GuardarConfig;
    ConsultarStatus;
    SetMonitoreo(True);
  end;
end;

procedure TfrmPrincipal.tmrSondeoTimer(Sender: TObject);
begin
  if FOcupado then
    Exit;
  FOcupado := True;
  Peticion('/data',
    procedure(Ok: Boolean; Codigo: Integer; const Cuerpo: string)
    begin
      FOcupado := False;
      if not FMonitoreando then
        Exit;
      if Ok then
      begin
        FFallosSeguidos := 0;
        ProcesarDatos(Cuerpo);
      end
      else
      begin
        Inc(FFallosSeguidos);
        if FFallosSeguidos >= 3 then
          MostrarConexion('Sin respuesta', COLOR_ERROR)
        else
          MostrarConexion('Reintentando...', COLOR_REPOSO);
      end;
    end);
end;

procedure TfrmPrincipal.ConsultarStatus;
begin
  Peticion('/status',
    procedure(Ok: Boolean; Codigo: Integer; const Cuerpo: string)
    begin
      if Ok then
        ProcesarStatus(Cuerpo);
    end);
end;

procedure TfrmPrincipal.btnDespertarClick(Sender: TObject);
begin
  MostrarConexion('Despertando...', COLOR_REPOSO);
  Peticion('/wake?min=5',
    procedure(Ok: Boolean; Codigo: Integer; const Cuerpo: string)
    begin
      if Ok then
      begin
        MostrarConexion('Activo', COLOR_OK);
        lblEnergia.Text := 'ESP32 despierto. Se mantendr'#225' activo al menos 5 min.';
        ConsultarStatus;
        if not FMonitoreando then
          SetMonitoreo(True);
      end
      else
      begin
        MostrarConexion('Sin respuesta', COLOR_ERROR);
        Aviso('No se pudo despertar al ESP32. Revisa la IP y que est'#233' en la misma red.');
      end;
    end);
end;

procedure TfrmPrincipal.btnDormirClick(Sender: TObject);
begin
  // Primero se detiene el sondeo; si no, la siguiente petición lo despertaría.
  SetMonitoreo(False);
  Peticion('/sleep',
    procedure(Ok: Boolean; Codigo: Integer; const Cuerpo: string)
    begin
      if Ok then
      begin
        MostrarConexion('En reposo', COLOR_REPOSO);
        lblEnergia.Text := 'ESP32 en reposo. Cualquier petici'#243'n lo despierta.';
      end
      else
        Aviso('No se pudo enviar la orden de dormir.');
    end);
end;

{ ---------- procesamiento ---------- }

procedure TfrmPrincipal.ProcesarStatus(const Cuerpo: string);
var
  V: TJSONValue;
  O: TJSONObject;
  Modo: string;
  Rssi: Double;
begin
  V := TJSONObject.ParseJSONValue(Cuerpo);
  try
    if not (V is TJSONObject) then
      Exit;
    O := TJSONObject(V);
    if JsonTexto(O, 'sleepMode') = 'light' then
      Modo := 'light sleep'
    else
      Modo := 'modem sleep';
    if not JsonNumero(O, 'rssi', Rssi) then
      Rssi := 0;
    lblCal.Text := Format('Calibraci'#243'n: vac'#237'o = %s, lleno = %s   |   FW %s',
      [JsonTexto(O, 'emptyAdc'), JsonTexto(O, 'fullAdc'), JsonTexto(O, 'fw')]);
    lblConexion.Text := Format('Conexi'#243'n con el ESP32  (%s, %d dBm, reposo: %s)',
      [JsonTexto(O, 'ssid'), Round(Rssi), Modo]);
  finally
    V.Free;
  end;
end;

procedure TfrmPrincipal.ProcesarDatos(const Cuerpo: string);
var
  V: TJSONValue;
  O: TJSONObject;
  L: TLectura;
  D, Restante: Double;
begin
  V := TJSONObject.ParseJSONValue(Cuerpo);
  try
    if not (V is TJSONObject) then
    begin
      MostrarConexion('Respuesta inv'#225'lida', COLOR_ERROR);
      Exit;
    end;
    O := TJSONObject(V);

    L.Hora := Now;
    if JsonNumero(O, 'adc', D) then L.Adc := Round(D) else L.Adc := 0;
    if not JsonNumero(O, 'voltage', L.Voltaje) then L.Voltaje := 0;
    if not JsonNumero(O, 'level', L.Nivel) then L.Nivel := 0;
    if not JsonNumero(O, 'levelRaw', L.NivelCrudo) then L.NivelCrudo := L.Nivel;
    L.TempValida := JsonNumero(O, 'temperatureC', L.Temp);
    L.Estado := JsonTexto(O, 'state');

    if not JsonNumero(O, 'awakeRemaining', Restante) then
      Restante := 0;
  finally
    V.Free;
  end;

  MostrarConexion('Activo', COLOR_OK);
  lblEnergia.Text := Format(
    'ESP32 activo. Entrar'#225' en reposo %d s despu'#233's de la '#250'ltima petici'#243'n.',
    [Round(Restante)]);

  FLecturas.Add(L);
  if FLecturas.Count > MAX_LECTURAS then
    FLecturas.Delete(0);

  MostrarTemperatura(L);
  MostrarNivel(L);
  AgregarHistorial(L);
  pbxGrafica.Repaint;
end;

procedure TfrmPrincipal.MostrarTemperatura(const L: TLectura);
var
  C: TAlphaColor;
  Texto: string;
begin
  if not L.TempValida then
  begin
    lblTemp.Text := '--';
    lblTemp.TextSettings.FontColor := COLOR_GRIS;
    lblEstadoTemp.Text := 'Sensor desconectado';
    rctEstadoTemp.Fill.Color := COLOR_GRIS;
    rctTermoRelleno.Width := 0;
    Exit;
  end;

  Texto := EstadoTemperatura(L.Temp, C);
  lblTemp.Text := Format('%.1f '#176'C', [L.Temp]);
  lblTemp.TextSettings.FontColor := C;
  lblEstadoTemp.Text := Texto;
  rctEstadoTemp.Fill.Color := C;
  rctTermoRelleno.Fill.Color := C;
  rctTermoRelleno.Width := rctTermoFondo.Width * EnsureRange(L.Temp, 0, 100) / 100;

  if FTempCuenta = 0 then
  begin
    FTempMin := L.Temp;
    FTempMax := L.Temp;
  end
  else
  begin
    FTempMin := Min(FTempMin, L.Temp);
    FTempMax := Max(FTempMax, L.Temp);
  end;
  FTempSuma := FTempSuma + L.Temp;
  Inc(FTempCuenta);

  lblMin.Text := Format('M'#237'n'#13#10'%.1f '#176'C', [FTempMin]);
  lblProm.Text := Format('Prom'#13#10'%.1f '#176'C', [FTempSuma / FTempCuenta]);
  lblMax.Text := Format('M'#225'x'#13#10'%.1f '#176'C', [FTempMax]);
end;

procedure TfrmPrincipal.MostrarNivel(const L: TLectura);
var
  Alto: Single;
begin
  lblNivel.Text := Format('%.1f %%', [L.Nivel]);
  lblNivelCrudo.Text := Format('Sin filtro: %.1f %%', [L.NivelCrudo]);
  lblAdc.Text := Format('ADC: %d', [L.Adc]);
  lblVoltaje.Text := Format('Voltaje: %.3f V', [L.Voltaje]);

  Alto := (rctTanque.Height - 6) * EnsureRange(L.Nivel, 0, 100) / 100;
  rctAgua.Height := Alto;
end;

procedure TfrmPrincipal.AgregarHistorial(const L: TLectura);
var
  Fila, T: string;
begin
  if L.TempValida then
    T := Format('%.1f '#176'C', [L.Temp])
  else
    T := '--';
  Fila := Format('%s   %4d   %5.1f %%   %s',
    [FormatDateTime('hh:nn:ss', L.Hora), L.Adc, L.Nivel, T]);

  lbHistorial.BeginUpdate;
  try
    lbHistorial.Items.Add(Fila);
    while lbHistorial.Count > MAX_HISTORIAL do
      lbHistorial.Items.Delete(0);
  finally
    lbHistorial.EndUpdate;
  end;
  lbHistorial.ItemIndex := lbHistorial.Count - 1;
  lbHistorial.ScrollToItem(lbHistorial.ListItems[lbHistorial.Count - 1]);
end;

{ ---------- gráfica ---------- }

procedure TfrmPrincipal.pbxGraficaPaint(Sender: TObject; Canvas: TCanvas);
var
  R: TRectF;
  N, Inicio, I: Integer;
  TMin, TMax, X, Y: Double;
  PathT, PathN: TPathData;
  Primero: Boolean;
  L: TLectura;
begin
  R := pbxGrafica.LocalRect;

  Canvas.Fill.Kind := TBrushKind.Solid;
  Canvas.Fill.Color := $FFFAFAFA;
  Canvas.FillRect(R, 8, 8, AllCorners, 1);
  Canvas.Stroke.Kind := TBrushKind.Solid;
  Canvas.Stroke.Color := $FFEEEEEE;
  Canvas.Stroke.Thickness := 1;
  Canvas.DrawRect(R, 8, 8, AllCorners, 1);

  N := Min(FLecturas.Count, MAX_GRAFICA);
  Canvas.Font.Size := 11;

  if N < 2 then
  begin
    Canvas.Fill.Color := $FF999999;
    Canvas.FillText(R, 'La gr'#225'fica aparece al recibir lecturas', False, 1,
      [], TTextAlign.Center, TTextAlign.Center);
    Exit;
  end;

  Inicio := FLecturas.Count - N;

  // Escala de temperatura
  TMin := 1000;
  TMax := -1000;
  for I := Inicio to FLecturas.Count - 1 do
    if FLecturas[I].TempValida then
    begin
      TMin := Min(TMin, FLecturas[I].Temp);
      TMax := Max(TMax, FLecturas[I].Temp);
    end;
  if TMax < TMin then
  begin
    TMin := 0;
    TMax := 100;
  end;
  if TMax - TMin < 1 then
  begin
    TMin := TMin - 0.5;
    TMax := TMax + 0.5;
  end;

  PathT := TPathData.Create;
  PathN := TPathData.Create;
  try
    Primero := True;
    for I := Inicio to FLecturas.Count - 1 do
    begin
      L := FLecturas[I];
      X := R.Left + 4 + (I - Inicio) / (N - 1) * (R.Width - 8);

      // Nivel (0-100 %), azul
      Y := R.Bottom - 8 - (EnsureRange(L.Nivel, 0, 100) / 100) * (R.Height - 22);
      if I = Inicio then
        PathN.MoveTo(PointF(X, Y))
      else
        PathN.LineTo(PointF(X, Y));

      // Temperatura (escala automática), naranja
      if L.TempValida then
      begin
        Y := R.Bottom - 8 - ((L.Temp - TMin) / (TMax - TMin)) * (R.Height - 22);
        if Primero then
          PathT.MoveTo(PointF(X, Y))
        else
          PathT.LineTo(PointF(X, Y));
        Primero := False;
      end;
    end;

    Canvas.Stroke.Thickness := 2;
    Canvas.Stroke.Color := COLOR_NIVEL;
    Canvas.DrawPath(PathN, 0.6);
    Canvas.Stroke.Color := COLOR_TEMP;
    Canvas.DrawPath(PathT, 1);
  finally
    PathT.Free;
    PathN.Free;
  end;

  Canvas.Fill.Color := COLOR_TEMP;
  Canvas.FillText(RectF(R.Left + 6, R.Top + 2, R.Right - 6, R.Top + 16),
    Format('%.1f '#176'C', [TMax]), False, 1, [], TTextAlign.Leading);
  Canvas.FillText(RectF(R.Left + 6, R.Bottom - 16, R.Right - 6, R.Bottom - 2),
    Format('%.1f '#176'C', [TMin]), False, 1, [], TTextAlign.Leading);
  Canvas.Fill.Color := COLOR_NIVEL;
  Canvas.FillText(RectF(R.Left + 6, R.Top + 2, R.Right - 6, R.Top + 16),
    'Nivel %', False, 1, [], TTextAlign.Trailing);
end;

{ ---------- calibración ---------- }

procedure TfrmPrincipal.Calibrar(const Punto, Descripcion: string);
begin
  FPuntoCal := Punto;
  TDialogService.MessageDialog(
    Format(#191'Guardar la lectura actual de la sonda como %s?'#13#10 +
           'Hazlo con el tanque realmente %s.', [Descripcion, Descripcion]),
    TMsgDlgType.mtConfirmation, [TMsgDlgBtn.mbYes, TMsgDlgBtn.mbNo],
    TMsgDlgBtn.mbNo, 0,
    procedure(const AResult: TModalResult)
    begin
      if AResult = mrYes then
        EnviarCalibracion;
    end);
end;

procedure TfrmPrincipal.EnviarCalibracion;
begin
  Peticion('/calibrate?point=' + FPuntoCal,
    procedure(Ok: Boolean; Codigo: Integer; const Cuerpo: string)
    begin
      if Ok then
      begin
        Aviso('Calibraci'#243'n guardada en el ESP32.');
        ConsultarStatus;
      end
      else
        Aviso('No se guard'#243' la calibraci'#243'n: ' + Cuerpo);
    end);
end;

procedure TfrmPrincipal.btnCalVacioClick(Sender: TObject);
begin
  Calibrar('empty', 'VAC'#205'O');
end;

procedure TfrmPrincipal.btnCalLlenoClick(Sender: TObject);
begin
  Calibrar('full', 'LLENO');
end;

{ ---------- historial ---------- }

procedure TfrmPrincipal.btnLimpiarClick(Sender: TObject);
begin
  FLecturas.Clear;
  lbHistorial.Clear;
  FTempCuenta := 0;
  FTempSuma := 0;
  lblMin.Text := 'M'#237'n --';
  lblProm.Text := 'Prom --';
  lblMax.Text := 'M'#225'x --';
  pbxGrafica.Repaint;
end;

procedure TfrmPrincipal.btnExportarClick(Sender: TObject);
var
  SL: TStringList;
  L: TLectura;
  FS: TFormatSettings;
  Ruta, T: string;
{$IFDEF MSWINDOWS}
  Dlg: TSaveDialog;
{$ENDIF}
begin
  if FLecturas.Count = 0 then
  begin
    Aviso('Todav'#237'a no hay datos.');
    Exit;
  end;

  FS := TFormatSettings.Invariant;
  SL := TStringList.Create;
  try
    SL.Add('Fecha,Hora,ADC,Voltaje (V),Nivel (%),Nivel sin filtro (%),Temperatura (C),Estado ESP32');
    for L in FLecturas do
    begin
      if L.TempValida then
        T := FormatFloat('0.00', L.Temp, FS)
      else
        T := '';
      SL.Add(string.Join(',', [
        FormatDateTime('yyyy-mm-dd', L.Hora),
        FormatDateTime('hh:nn:ss', L.Hora),
        IntToStr(L.Adc),
        FormatFloat('0.000', L.Voltaje, FS),
        FormatFloat('0.00', L.Nivel, FS),
        FormatFloat('0.00', L.NivelCrudo, FS),
        T,
        L.Estado]));
    end;

    Ruta := 'calentador_solar_' + FormatDateTime('yyyymmdd_hhnnss', Now) + '.csv';

{$IFDEF MSWINDOWS}
    Dlg := TSaveDialog.Create(nil);
    try
      Dlg.Filter := 'Archivo CSV (*.csv)|*.csv';
      Dlg.DefaultExt := 'csv';
      Dlg.FileName := Ruta;
      if not Dlg.Execute then
        Exit;
      Ruta := Dlg.FileName;
    finally
      Dlg.Free;
    end;
{$ELSE}
    // Android: carpeta de la app accesible por USB
    // (Android/data/<paquete>/files), no requiere permisos.
    Ruta := System.IOUtils.TPath.Combine(System.IOUtils.TPath.GetPublicPath, Ruta);
{$ENDIF}

    SL.SaveToFile(Ruta, TEncoding.UTF8);  // UTF-8 con BOM: Excel lo abre bien
    Aviso('CSV guardado en:'#13#10 + Ruta);
  finally
    SL.Free;
  end;
end;

procedure TfrmPrincipal.Aviso(const Msg: string);
begin
  TDialogService.ShowMessage(Msg);
end;

end.
