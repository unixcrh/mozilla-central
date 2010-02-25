/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsIFormSubmission.h"

#include "nsPresContext.h"
#include "nsCOMPtr.h"
#include "nsIForm.h"
#include "nsILinkHandler.h"
#include "nsIDocument.h"
#include "nsGkAtoms.h"
#include "nsIHTMLDocument.h"
#include "nsIFormControl.h"
#include "nsIDOMHTMLFormElement.h"
#include "nsDOMError.h"
#include "nsGenericHTMLElement.h"
#include "nsISaveAsCharset.h"

// JBK added for submit move from content frame
#include "nsIFile.h"
#include "nsDirectoryServiceDefs.h"
#include "nsStringStream.h"
#include "nsIURI.h"
#include "nsIURL.h"
#include "nsNetUtil.h"
#include "nsLinebreakConverter.h"
#include "nsICharsetConverterManager.h"
#include "nsICharsetAlias.h"
#include "nsEscape.h"
#include "nsUnicharUtils.h"
#include "nsIMultiplexInputStream.h"
#include "nsIMIMEInputStream.h"
#include "nsIMIMEService.h"
#include "nsIConsoleService.h"
#include "nsIScriptError.h"
#include "nsIStringBundle.h"
#include "nsCExternalHandlerService.h"
#include "nsIFileStreams.h"
#include "nsBidiUtils.h"

static void
SendJSWarning(nsIDocument* aDocument,
              const char* aWarningName,
              const PRUnichar** aWarningArgs, PRUint32 aWarningArgsLen)
{
  nsContentUtils::ReportToConsole(nsContentUtils::eFORMS_PROPERTIES,
                                  aWarningName,
                                  aWarningArgs, aWarningArgsLen,
                                  aDocument ? aDocument->GetDocumentURI() :
                                              nsnull,
                                  EmptyString(), 0, 0,
                                  nsIScriptError::warningFlag,
                                  "HTML");
}

// --------------------------------------------------------------------------

class nsFSURLEncoded : public nsFormSubmission
{
public:
  /**
   * @param aCharset the charset of the form as a string
   * @param aEncoder an encoder that will encode Unicode names and values into
   *        bytes to be sent over the wire (usually a charset transformation)
   * @param aBidiOptions the BIDI options flags for the current pres context
   * @param aMethod the method of the submit (either NS_FORM_METHOD_GET or
   *        NS_FORM_METHOD_POST).
   */
  nsFSURLEncoded(const nsACString& aCharset,
                 nsISaveAsCharset* aEncoder,
                 PRInt32 aBidiOptions,
                 PRInt32 aMethod,
                 nsIDocument* aDocument)
    : nsFormSubmission(aCharset, aEncoder, aBidiOptions),
      mMethod(aMethod),
      mDocument(aDocument),
      mWarnedFileControl(PR_FALSE)
  {
  }

  virtual nsresult AddNameValuePair(const nsAString& aName,
                                    const nsAString& aValue);
  virtual nsresult AddNameFilePair(const nsAString& aName,
                                   nsIFile* aFile);

protected:
  NS_IMETHOD GetEncodedSubmission(nsIURI* aURI,
                                  nsIInputStream** aPostDataStream);

  /**
   * URL encode a Unicode string by encoding it to bytes, converting linebreaks
   * properly, and then escaping many bytes as %xx.
   *
   * @param aStr the string to encode
   * @param aEncoded the encoded string [OUT]
   * @throws NS_ERROR_OUT_OF_MEMORY if we run out of memory
   */
  nsresult URLEncode(const nsAString& aStr, nsCString& aEncoded);

private:
  /**
   * The method of the submit (either NS_FORM_METHOD_GET or
   * NS_FORM_METHOD_POST).
   */
  PRInt32 mMethod;

  /** The query string so far (the part after the ?) */
  nsCString mQueryString;

  /** The document whose URI to use when reporting errors */
  nsCOMPtr<nsIDocument> mDocument;

  /** Whether or not we have warned about a file control not being submitted */
  PRBool mWarnedFileControl;
};

nsresult
nsFSURLEncoded::AddNameValuePair(const nsAString& aName,
                                 const nsAString& aValue)
{
  // Encode value
  nsCString convValue;
  nsresult rv = URLEncode(aValue, convValue);
  NS_ENSURE_SUCCESS(rv, rv);

  // Encode name
  nsCAutoString convName;
  rv = URLEncode(aName, convName);
  NS_ENSURE_SUCCESS(rv, rv);


  // Append data to string
  if (mQueryString.IsEmpty()) {
    mQueryString += convName + NS_LITERAL_CSTRING("=") + convValue;
  } else {
    mQueryString += NS_LITERAL_CSTRING("&") + convName
                  + NS_LITERAL_CSTRING("=") + convValue;
  }

  return NS_OK;
}

nsresult
nsFSURLEncoded::AddNameFilePair(const nsAString& aName,
                                nsIFile* aFile)
{
  if (!mWarnedFileControl) {
    SendJSWarning(mDocument, "ForgotFileEnctypeWarning", nsnull, 0);
    mWarnedFileControl = PR_TRUE;
  }

  nsAutoString filename;
  if (aFile) {
    aFile->GetLeafName(filename);
  }

  return AddNameValuePair(aName, filename);
}

static void
HandleMailtoSubject(nsCString& aPath) {

  // Walk through the string and see if we have a subject already.
  PRBool hasSubject = PR_FALSE;
  PRBool hasParams = PR_FALSE;
  PRInt32 paramSep = aPath.FindChar('?');
  while (paramSep != kNotFound && paramSep < (PRInt32)aPath.Length()) {
    hasParams = PR_TRUE;

    // Get the end of the name at the = op.  If it is *after* the next &,
    // assume that someone made a parameter without an = in it
    PRInt32 nameEnd = aPath.FindChar('=', paramSep+1);
    PRInt32 nextParamSep = aPath.FindChar('&', paramSep+1);
    if (nextParamSep == kNotFound) {
      nextParamSep = aPath.Length();
    }

    // If the = op is after the &, this parameter is a name without value.
    // If there is no = op, same thing.
    if (nameEnd == kNotFound || nextParamSep < nameEnd) {
      nameEnd = nextParamSep;
    }

    if (nameEnd != kNotFound) {
      if (Substring(aPath, paramSep+1, nameEnd-(paramSep+1)).
          LowerCaseEqualsLiteral("subject")) {
        hasSubject = PR_TRUE;
        break;
      }
    }

    paramSep = nextParamSep;
  }

  // If there is no subject, append a preformed subject to the mailto line
  if (!hasSubject) {
    if (hasParams) {
      aPath.Append('&');
    } else {
      aPath.Append('?');
    }

    // Get the default subject
    nsXPIDLString brandName;
    nsresult rv =
      nsContentUtils::GetLocalizedString(nsContentUtils::eBRAND_PROPERTIES,
                                         "brandShortName", brandName);
    if (NS_FAILED(rv))
      return;
    const PRUnichar *formatStrings[] = { brandName.get() };
    nsXPIDLString subjectStr;
    rv = nsContentUtils::FormatLocalizedString(
                                           nsContentUtils::eFORMS_PROPERTIES,
                                           "DefaultFormSubject",
                                           formatStrings,
                                           NS_ARRAY_LENGTH(formatStrings),
                                           subjectStr);
    if (NS_FAILED(rv))
      return;
    aPath.AppendLiteral("subject=");
    nsCString subjectStrEscaped;
    aPath.Append(NS_EscapeURL(NS_ConvertUTF16toUTF8(subjectStr), esc_Query,
                              subjectStrEscaped));
  }
}

NS_IMETHODIMP
nsFSURLEncoded::GetEncodedSubmission(nsIURI* aURI,
                                     nsIInputStream** aPostDataStream)
{
  nsresult rv = NS_OK;

  *aPostDataStream = nsnull;

  if (mMethod == NS_FORM_METHOD_POST) {

    PRBool isMailto = PR_FALSE;
    aURI->SchemeIs("mailto", &isMailto);
    if (isMailto) {

      nsCAutoString path;
      rv = aURI->GetPath(path);
      NS_ENSURE_SUCCESS(rv, rv);

      HandleMailtoSubject(path);

      // Append the body to and force-plain-text args to the mailto line
      nsCString escapedBody;
      escapedBody.Adopt(nsEscape(mQueryString.get(), url_XAlphas));

      path += NS_LITERAL_CSTRING("&force-plain-text=Y&body=") + escapedBody;

      rv = aURI->SetPath(path);

    } else {

      nsCOMPtr<nsIInputStream> dataStream;
      // XXX We *really* need to either get the string to disown its data (and
      // not destroy it), or make a string input stream that owns the CString
      // that is passed to it.  Right now this operation does a copy.
      rv = NS_NewCStringInputStream(getter_AddRefs(dataStream), mQueryString);
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<nsIMIMEInputStream> mimeStream(
        do_CreateInstance("@mozilla.org/network/mime-input-stream;1", &rv));
      NS_ENSURE_SUCCESS(rv, rv);

#ifdef SPECIFY_CHARSET_IN_CONTENT_TYPE
      mimeStream->AddHeader("Content-Type",
                            PromiseFlatString(
                              "application/x-www-form-urlencoded; charset="
                              + mCharset
                            ).get());
#else
      mimeStream->AddHeader("Content-Type",
                            "application/x-www-form-urlencoded");
#endif
      mimeStream->SetAddContentLength(PR_TRUE);
      mimeStream->SetData(dataStream);

      *aPostDataStream = mimeStream;
      NS_ADDREF(*aPostDataStream);
    }

  } else {
    // Get the full query string
    PRBool schemeIsJavaScript;
    rv = aURI->SchemeIs("javascript", &schemeIsJavaScript);
    NS_ENSURE_SUCCESS(rv, rv);
    if (schemeIsJavaScript) {
      return NS_OK;
    }

    nsCOMPtr<nsIURL> url = do_QueryInterface(aURI);
    if (url) {
      url->SetQuery(mQueryString);
    }
    else {
      nsCAutoString path;
      rv = aURI->GetPath(path);
      NS_ENSURE_SUCCESS(rv, rv);
      // Bug 42616: Trim off named anchor and save it to add later
      PRInt32 namedAnchorPos = path.FindChar('#');
      nsCAutoString namedAnchor;
      if (kNotFound != namedAnchorPos) {
        path.Right(namedAnchor, (path.Length() - namedAnchorPos));
        path.Truncate(namedAnchorPos);
      }

      // Chop off old query string (bug 25330, 57333)
      // Only do this for GET not POST (bug 41585)
      PRInt32 queryStart = path.FindChar('?');
      if (kNotFound != queryStart) {
        path.Truncate(queryStart);
      }

      path.Append('?');
      // Bug 42616: Add named anchor to end after query string
      path.Append(mQueryString + namedAnchor);

      aURI->SetPath(path);
    }
  }

  return rv;
}

// i18n helper routines
nsresult
nsFSURLEncoded::URLEncode(const nsAString& aStr, nsCString& aEncoded)
{
  // convert to CRLF breaks
  PRUnichar* convertedBuf =
    nsLinebreakConverter::ConvertUnicharLineBreaks(PromiseFlatString(aStr).get(),
                                                   nsLinebreakConverter::eLinebreakAny,
                                                   nsLinebreakConverter::eLinebreakNet);
  NS_ENSURE_TRUE(convertedBuf, NS_ERROR_OUT_OF_MEMORY);

  nsCAutoString encodedBuf;
  nsresult rv = EncodeVal(nsDependentString(convertedBuf), encodedBuf);
  nsMemory::Free(convertedBuf);
  NS_ENSURE_SUCCESS(rv, rv);

  char* escapedBuf = nsEscape(encodedBuf.get(), url_XPAlphas);
  NS_ENSURE_TRUE(escapedBuf, NS_ERROR_OUT_OF_MEMORY);
  aEncoded.Adopt(escapedBuf);

  return NS_OK;
}

// --------------------------------------------------------------------------

/**
 * Handle multipart/form-data encoding, which does files as well as normal
 * inputs.  This always does POST.
 */
class nsFSMultipartFormData : public nsFormSubmission
{
public:
  /**
   * @param aCharset the charset of the form as a string
   * @param aEncoder an encoder that will encode Unicode names and values into
   *        bytes to be sent over the wire (usually a charset transformation)
   * @param aBidiOptions the BIDI options flags for the current pres context
   */
  nsFSMultipartFormData(const nsACString& aCharset,
                        nsISaveAsCharset* aEncoder,
                        PRInt32 aBidiOptions);
 
  virtual nsresult AddNameValuePair(const nsAString& aName,
                                    const nsAString& aValue);
  virtual nsresult AddNameFilePair(const nsAString& aName,
                                   nsIFile* aFile);

protected:
  NS_IMETHOD GetEncodedSubmission(nsIURI* aURI,
                                  nsIInputStream** aPostDataStream);

  /**
   * Roll up the data we have so far and add it to the multiplexed data stream.
   */
  nsresult AddPostDataStream();

private:
  /**
   * The post data stream as it is so far.  This is a collection of smaller
   * chunks--string streams and file streams interleaved to make one big POST
   * stream.
   */
  nsCOMPtr<nsIMultiplexInputStream> mPostDataStream;

  /**
   * The current string chunk.  When a file is hit, the string chunk gets
   * wrapped up into an input stream and put into mPostDataStream so that the
   * file input stream can then be appended and everything is in the right
   * order.  Then the string chunk gets appended to again as we process more
   * name/value pairs.
   */
  nsCString mPostDataChunk;

  /**
   * The boundary string to use after each "part" (the boundary that marks the
   * end of a value).  This is computed randomly and is different for each
   * submission.
   */
  nsCString mBoundary;
};

nsFSMultipartFormData::nsFSMultipartFormData(const nsACString& aCharset,
                                             nsISaveAsCharset* aEncoder,
                                             PRInt32 aBidiOptions)
    : nsFormSubmission(aCharset, aEncoder, aBidiOptions)
{
  mPostDataStream =
    do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1");

  mBoundary.AssignLiteral("---------------------------");
  mBoundary.AppendInt(rand());
  mBoundary.AppendInt(rand());
  mBoundary.AppendInt(rand());
}

nsresult
nsFSMultipartFormData::AddNameValuePair(const nsAString& aName,
                                        const nsAString& aValue)
{
  nsCString valueStr;
  nsCAutoString encodedVal;
  nsresult rv = EncodeVal(aValue, encodedVal);
  NS_ENSURE_SUCCESS(rv, rv);

  valueStr.Adopt(nsLinebreakConverter::
                 ConvertLineBreaks(encodedVal.get(),
                                   nsLinebreakConverter::eLinebreakAny,
                                   nsLinebreakConverter::eLinebreakNet));

  nsCAutoString nameStr;
  rv = EncodeVal(aName, nameStr);
  NS_ENSURE_SUCCESS(rv, rv);

  // Make MIME block for name/value pair

  // XXX: name parameter should be encoded per RFC 2231
  // RFC 2388 specifies that RFC 2047 be used, but I think it's not 
  // consistent with MIME standard.
  mPostDataChunk += NS_LITERAL_CSTRING("--") + mBoundary
                 + NS_LITERAL_CSTRING(CRLF)
                 + NS_LITERAL_CSTRING("Content-Disposition: form-data; name=\"")
                 + nameStr + NS_LITERAL_CSTRING("\"" CRLF CRLF)
                 + valueStr + NS_LITERAL_CSTRING(CRLF);

  return NS_OK;
}

nsresult
nsFSMultipartFormData::AddNameFilePair(const nsAString& aName,
                                       nsIFile* aFile)
{
  // Encode the control name
  nsCAutoString nameStr;
  nsresult rv = EncodeVal(aName, nameStr);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString filenameStr;
  nsCAutoString contentType;
  nsCOMPtr<nsIInputStream> fileStream;
  if (aFile) {
    // Get and encode the filename
    nsAutoString filename;
    aFile->GetLeafName(filename);
    nsCAutoString encodedFileName;
    rv = EncodeVal(filename, encodedFileName);
    NS_ENSURE_SUCCESS(rv, rv);
  
    filenameStr.Adopt(nsLinebreakConverter::
                      ConvertLineBreaks(encodedFileName.get(),
                                        nsLinebreakConverter::eLinebreakAny,
                                        nsLinebreakConverter::eLinebreakNet));
  
    // Get content type
    nsCOMPtr<nsIMIMEService> MIMEService =
      do_GetService(NS_MIMESERVICE_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = MIMEService->GetTypeFromFile(aFile, contentType);
    if (NS_FAILED(rv)) {
      contentType.AssignLiteral("application/octet-stream");
    }
  
    // Get input stream
    rv = NS_NewLocalFileInputStream(getter_AddRefs(fileStream),
                                    aFile, -1, -1,
                                    nsIFileInputStream::CLOSE_ON_EOF |
                                    nsIFileInputStream::REOPEN_ON_REWIND);
    if (fileStream) {
      // Create buffered stream (for efficiency)
      nsCOMPtr<nsIInputStream> bufferedStream;
      rv = NS_NewBufferedInputStream(getter_AddRefs(bufferedStream),
                                     fileStream, 8192);
      NS_ENSURE_SUCCESS(rv, rv);
  
      fileStream = bufferedStream;
    }
  }
  else {
    contentType.AssignLiteral("application/octet-stream");
  }

  //
  // Make MIME block for name/value pair
  //
  // more appropriate than always using binary?
  mPostDataChunk += NS_LITERAL_CSTRING("--") + mBoundary
                 + NS_LITERAL_CSTRING(CRLF);
  // XXX: name/filename parameter should be encoded per RFC 2231
  // RFC 2388 specifies that RFC 2047 be used, but I think it's not 
  // consistent with the MIME standard.
  mPostDataChunk +=
         NS_LITERAL_CSTRING("Content-Disposition: form-data; name=\"")
       + nameStr + NS_LITERAL_CSTRING("\"; filename=\"")
       + filenameStr + NS_LITERAL_CSTRING("\"" CRLF)
       + NS_LITERAL_CSTRING("Content-Type: ") + contentType
       + NS_LITERAL_CSTRING(CRLF CRLF);

  // Add the file to the stream
  if (fileStream) {
    // We need to dump the data up to this point into the POST data stream here,
    // since we're about to add the file input stream
    AddPostDataStream();

    mPostDataStream->AppendStream(fileStream);
  }

  // CRLF after file
  mPostDataChunk.AppendLiteral(CRLF);

  return NS_OK;
}

NS_IMETHODIMP
nsFSMultipartFormData::GetEncodedSubmission(nsIURI* aURI,
                                            nsIInputStream** aPostDataStream)
{
  nsresult rv;

  // Finish data
  mPostDataChunk += NS_LITERAL_CSTRING("--") + mBoundary
                  + NS_LITERAL_CSTRING("--" CRLF);

  // Add final data input stream
  AddPostDataStream();

  // Make header
  nsCOMPtr<nsIMIMEInputStream> mimeStream
    = do_CreateInstance("@mozilla.org/network/mime-input-stream;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString boundaryHeaderValue(
    NS_LITERAL_CSTRING("multipart/form-data; boundary=") + mBoundary);

  mimeStream->AddHeader("Content-Type", boundaryHeaderValue.get());
  mimeStream->SetAddContentLength(PR_TRUE);
  mimeStream->SetData(mPostDataStream);

  *aPostDataStream = mimeStream;

  NS_ADDREF(*aPostDataStream);

  return NS_OK;
}

nsresult
nsFSMultipartFormData::AddPostDataStream()
{
  nsresult rv = NS_OK;
  
  nsCOMPtr<nsIInputStream> postDataChunkStream;
  rv = NS_NewCStringInputStream(getter_AddRefs(postDataChunkStream),
                                mPostDataChunk);
  NS_ASSERTION(postDataChunkStream, "Could not open a stream for POST!");
  if (postDataChunkStream) {
    mPostDataStream->AppendStream(postDataChunkStream);
  }

  mPostDataChunk.Truncate();

  return rv;
}

// --------------------------------------------------------------------------

class nsFSTextPlain : public nsFormSubmission
{
public:
  nsFSTextPlain(const nsACString& aCharset,
                nsISaveAsCharset* aEncoder,
                PRInt32 aBidiOptions)
    : nsFormSubmission(aCharset, aEncoder, aBidiOptions)
  {
  }

  virtual nsresult AddNameValuePair(const nsAString& aName,
                                    const nsAString& aValue);
  virtual nsresult AddNameFilePair(const nsAString& aName,
                                   nsIFile* aFile);

protected:
  NS_IMETHOD GetEncodedSubmission(nsIURI* aURI,
                                  nsIInputStream** aPostDataStream);

private:
  nsString mBody;
};

nsresult
nsFSTextPlain::AddNameValuePair(const nsAString& aName,
                                const nsAString& aValue)
{
  // XXX This won't work well with a name like "a=b" or "a\nb" but I suppose
  // text/plain doesn't care about that.  Parsers aren't built for escaped
  // values so we'll have to live with it.
  mBody.Append(aName + NS_LITERAL_STRING("=") + aValue +
               NS_LITERAL_STRING(CRLF));

  return NS_OK;
}

nsresult
nsFSTextPlain::AddNameFilePair(const nsAString& aName,
                               nsIFile* aFile)
{
  nsAutoString filename;
  if (aFile) {
    aFile->GetLeafName(filename);
  }
    
  AddNameValuePair(aName, filename);
  return NS_OK;
}

NS_IMETHODIMP
nsFSTextPlain::GetEncodedSubmission(nsIURI* aURI,
                                    nsIInputStream** aPostDataStream)
{
  nsresult rv = NS_OK;

  // XXX HACK We are using the standard URL mechanism to give the body to the
  // mailer instead of passing the post data stream to it, since that sounds
  // hard.
  PRBool isMailto = PR_FALSE;
  aURI->SchemeIs("mailto", &isMailto);
  if (isMailto) {
    nsCAutoString path;
    rv = aURI->GetPath(path);
    NS_ENSURE_SUCCESS(rv, rv);

    HandleMailtoSubject(path);

    // Append the body to and force-plain-text args to the mailto line
    char* escapedBuf = nsEscape(NS_ConvertUTF16toUTF8(mBody).get(),
                                url_XAlphas);
    NS_ENSURE_TRUE(escapedBuf, NS_ERROR_OUT_OF_MEMORY);
    nsCString escapedBody;
    escapedBody.Adopt(escapedBuf);

    path += NS_LITERAL_CSTRING("&force-plain-text=Y&body=") + escapedBody;

    rv = aURI->SetPath(path);

  } else {
    // Create data stream
    nsCOMPtr<nsIInputStream> bodyStream;
    rv = NS_NewStringInputStream(getter_AddRefs(bodyStream),
                                          mBody);
    if (!bodyStream) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    // Create mime stream with headers and such
    nsCOMPtr<nsIMIMEInputStream> mimeStream
        = do_CreateInstance("@mozilla.org/network/mime-input-stream;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    mimeStream->AddHeader("Content-Type", "text/plain");
    mimeStream->SetAddContentLength(PR_TRUE);
    mimeStream->SetData(bodyStream);
    CallQueryInterface(mimeStream, aPostDataStream);
  }

  return rv;
}

// --------------------------------------------------------------------------

nsFormSubmission::nsFormSubmission(const nsACString& aCharset,
                                   nsISaveAsCharset* aEncoder,
                                   PRInt32 aBidiOptions)
  : mCharset(aCharset),
    mEncoder(aEncoder),
    mBidiOptions(aBidiOptions)
{
}

nsFormSubmission::~nsFormSubmission()
{
}

void
nsFormSubmission::Release()
{
  --mRefCnt;
  NS_LOG_RELEASE(this, mRefCnt, "nsFormSubmission");
  if (mRefCnt == 0) {
    mRefCnt = 1; // stabilize
    delete this;
  }
}

nsresult
nsFormSubmission::SubmitTo(nsIURI* aActionURI, const nsAString& aTarget,
                           nsIContent* aSource, nsILinkHandler* aLinkHandler,
                           nsIDocShell** aDocShell, nsIRequest** aRequest)
{
  nsresult rv;

  // Finish encoding (get post data stream and URI)
  nsCOMPtr<nsIInputStream> postDataStream;
  rv = GetEncodedSubmission(aActionURI, getter_AddRefs(postDataStream));
  NS_ENSURE_SUCCESS(rv, rv);

  // Actually submit the data
  NS_ENSURE_ARG_POINTER(aLinkHandler);

  return aLinkHandler->OnLinkClickSync(aSource, aActionURI,
                                       PromiseFlatString(aTarget).get(),
                                       postDataStream, nsnull,
                                       aDocShell, aRequest);
}

// i18n helper routines
nsresult
nsFormSubmission::UnicodeToNewBytes(const nsAString& aStr, 
                                    nsISaveAsCharset* aEncoder,
                                    nsACString& aOut)
{
  PRUint8 ctrlsModAtSubmit = GET_BIDI_OPTION_CONTROLSTEXTMODE(mBidiOptions);
  PRUint8 textDirAtSubmit = GET_BIDI_OPTION_DIRECTION(mBidiOptions);
  //ahmed 15-1
  nsAutoString newBuffer;
  //This condition handle the RTL,LTR for a logical file
  if (ctrlsModAtSubmit == IBMBIDI_CONTROLSTEXTMODE_VISUAL
     && mCharset.Equals(NS_LITERAL_CSTRING("windows-1256"),
                        nsCaseInsensitiveCStringComparator())) {
    Conv_06_FE_WithReverse(nsString(aStr),
                           newBuffer,
                           textDirAtSubmit);
  }
  else if (ctrlsModAtSubmit == IBMBIDI_CONTROLSTEXTMODE_LOGICAL
          && mCharset.Equals(NS_LITERAL_CSTRING("IBM864"),
                             nsCaseInsensitiveCStringComparator())) {
    //For 864 file, When it is logical, if LTR then only convert
    //If RTL will mak a reverse for the buffer
    Conv_FE_06(nsString(aStr), newBuffer);
    if (textDirAtSubmit == 2) { //RTL
    //Now we need to reverse the Buffer, it is by searching the buffer
      PRInt32 len = newBuffer.Length();
      PRUint32 z = 0;
      nsAutoString temp;
      temp.SetLength(len);
      while (--len >= 0)
        temp.SetCharAt(newBuffer.CharAt(len), z++);
      newBuffer = temp;
    }
  }
  else if (ctrlsModAtSubmit == IBMBIDI_CONTROLSTEXTMODE_VISUAL
          && mCharset.Equals(NS_LITERAL_CSTRING("IBM864"),
                             nsCaseInsensitiveCStringComparator())
                  && textDirAtSubmit == IBMBIDI_TEXTDIRECTION_RTL) {

    Conv_FE_06(nsString(aStr), newBuffer);
    //Now we need to reverse the Buffer, it is by searching the buffer
    PRInt32 len = newBuffer.Length();
    PRUint32 z = 0;
    nsAutoString temp;
    temp.SetLength(len);
    while (--len >= 0)
      temp.SetCharAt(newBuffer.CharAt(len), z++);
    newBuffer = temp;
  }
  else {
    newBuffer = aStr;
  }

  nsXPIDLCString res;
  if (!newBuffer.IsEmpty()) {
    aOut.Truncate();
    nsresult rv = aEncoder->Convert(newBuffer.get(), getter_Copies(res));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  aOut = res;
  return NS_OK;
}

nsresult
nsFormSubmission::EncodeVal(const nsAString& aStr, nsACString& aOut)
{
  NS_ASSERTION(mEncoder, "Encoder not available. Losing data !");
  if (mEncoder)
    return UnicodeToNewBytes(aStr, mEncoder, aOut);

  // fall back to UTF-8
  CopyUTF16toUTF8(aStr, aOut);
  return NS_OK;
}

// --------------------------------------------------------------------------

static void
GetSubmitCharset(nsGenericHTMLElement* aForm,
                 PRUint8 aCtrlsModAtSubmit,
                 nsACString& oCharset)
{
  oCharset.AssignLiteral("UTF-8"); // default to utf-8

  nsresult rv = NS_OK;
  nsAutoString acceptCharsetValue;
  aForm->GetAttr(kNameSpaceID_None, nsGkAtoms::acceptcharset,
                 acceptCharsetValue);

  PRInt32 charsetLen = acceptCharsetValue.Length();
  if (charsetLen > 0) {
    PRInt32 offset=0;
    PRInt32 spPos=0;
    // get charset from charsets one by one
    nsCOMPtr<nsICharsetAlias> calias(do_GetService(NS_CHARSETALIAS_CONTRACTID, &rv));
    if (NS_FAILED(rv)) {
      return;
    }
    if (calias) {
      do {
        spPos = acceptCharsetValue.FindChar(PRUnichar(' '), offset);
        PRInt32 cnt = ((-1==spPos)?(charsetLen-offset):(spPos-offset));
        if (cnt > 0) {
          nsAutoString uCharset;
          acceptCharsetValue.Mid(uCharset, offset, cnt);

          if (NS_SUCCEEDED(calias->
                           GetPreferred(NS_LossyConvertUTF16toASCII(uCharset),
                                        oCharset)))
            return;
        }
        offset = spPos + 1;
      } while (spPos != -1);
    }
  }
  // if there are no accept-charset or all the charset are not supported
  // Get the charset from document
  nsIDocument* doc = aForm->GetDocument();
  if (doc) {
    oCharset = doc->GetDocumentCharacterSet();
  }

  if (aCtrlsModAtSubmit==IBMBIDI_CONTROLSTEXTMODE_VISUAL
     && oCharset.Equals(NS_LITERAL_CSTRING("windows-1256"),
                        nsCaseInsensitiveCStringComparator())) {
    oCharset.AssignLiteral("IBM864");
  }
  else if (aCtrlsModAtSubmit==IBMBIDI_CONTROLSTEXTMODE_LOGICAL
          && oCharset.Equals(NS_LITERAL_CSTRING("IBM864"),
                             nsCaseInsensitiveCStringComparator())) {
    oCharset.AssignLiteral("IBM864i");
  }
  else if (aCtrlsModAtSubmit==IBMBIDI_CONTROLSTEXTMODE_VISUAL
          && oCharset.Equals(NS_LITERAL_CSTRING("ISO-8859-6"),
                             nsCaseInsensitiveCStringComparator())) {
    oCharset.AssignLiteral("IBM864");
  }
  else if (aCtrlsModAtSubmit==IBMBIDI_CONTROLSTEXTMODE_VISUAL
          && oCharset.Equals(NS_LITERAL_CSTRING("UTF-8"),
                             nsCaseInsensitiveCStringComparator())) {
    oCharset.AssignLiteral("IBM864");
  }
}

static nsresult
GetEncoder(nsGenericHTMLElement* aForm,
           const nsACString& aCharset,
           nsISaveAsCharset** aEncoder)
{
  *aEncoder = nsnull;
  nsresult rv = NS_OK;

  nsCAutoString charset(aCharset);
  // canonical name is passed so that we just have to check against
  // *our* canonical names listed in charsetaliases.properties
  if (charset.EqualsLiteral("ISO-8859-1")) {
    charset.AssignLiteral("windows-1252");
  }

  // use UTF-8 for UTF-16* and UTF-32* (per WHATWG and existing practice of
  // MS IE/Opera). 
  if (StringBeginsWith(charset, NS_LITERAL_CSTRING("UTF-16")) || 
      StringBeginsWith(charset, NS_LITERAL_CSTRING("UTF-32"))) {
    charset.AssignLiteral("UTF-8");
  }

  rv = CallCreateInstance( NS_SAVEASCHARSET_CONTRACTID, aEncoder);
  NS_ASSERTION(NS_SUCCEEDED(rv), "create nsISaveAsCharset failed");
  NS_ENSURE_SUCCESS(rv, rv);

  rv = (*aEncoder)->Init(charset.get(),
                         (nsISaveAsCharset::attr_EntityAfterCharsetConv + 
                          nsISaveAsCharset::attr_FallbackDecimalNCR),
                         0);
  NS_ASSERTION(NS_SUCCEEDED(rv), "initialize nsISaveAsCharset failed");
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

static void
GetEnumAttr(nsGenericHTMLElement* aContent,
            nsIAtom* atom, PRInt32* aValue)
{
  const nsAttrValue* value = aContent->GetParsedAttr(atom);
  if (value && value->Type() == nsAttrValue::eEnum) {
    *aValue = value->GetEnumValue();
  }
}

nsresult
GetSubmissionFromForm(nsGenericHTMLElement* aForm,
                      nsFormSubmission** aFormSubmission)
{
  // Get all the information necessary to encode the form data
  nsIDocument* doc = aForm->GetCurrentDoc();
  NS_ASSERTION(doc, "Should have doc if we're building submission!");

  // Get BIDI options
  PRUint8 ctrlsModAtSubmit = 0;
  PRUint32 bidiOptions = doc->GetBidiOptions();
  ctrlsModAtSubmit = GET_BIDI_OPTION_CONTROLSTEXTMODE(bidiOptions);

  // Get encoding type (default: urlencoded)
  PRInt32 enctype = NS_FORM_ENCTYPE_URLENCODED;
  GetEnumAttr(aForm, nsGkAtoms::enctype, &enctype);

  // Get method (default: GET)
  PRInt32 method = NS_FORM_METHOD_GET;
  GetEnumAttr(aForm, nsGkAtoms::method, &method);

  // Get charset
  nsCAutoString charset;
  GetSubmitCharset(aForm, ctrlsModAtSubmit, charset);

  // Get unicode encoder
  nsCOMPtr<nsISaveAsCharset> encoder;
  GetEncoder(aForm, charset, getter_AddRefs(encoder));

  // Choose encoder
  if (method == NS_FORM_METHOD_POST &&
      enctype == NS_FORM_ENCTYPE_MULTIPART) {
    *aFormSubmission = new nsFSMultipartFormData(charset, encoder,
                                                 bidiOptions);
  } else if (method == NS_FORM_METHOD_POST &&
             enctype == NS_FORM_ENCTYPE_TEXTPLAIN) {
    *aFormSubmission = new nsFSTextPlain(charset, encoder, bidiOptions);
  } else {
    if (enctype == NS_FORM_ENCTYPE_MULTIPART ||
        enctype == NS_FORM_ENCTYPE_TEXTPLAIN) {
      nsAutoString enctypeStr;
      aForm->GetAttr(kNameSpaceID_None, nsGkAtoms::enctype, enctypeStr);
      const PRUnichar* enctypeStrPtr = enctypeStr.get();
      SendJSWarning(aForm->GetOwnerDoc(), "ForgotPostWarning",
                    &enctypeStrPtr, 1);
    }
    *aFormSubmission = new nsFSURLEncoded(charset, encoder, bidiOptions,
                                          method, aForm->GetOwnerDoc());
  }
  NS_ENSURE_TRUE(*aFormSubmission, NS_ERROR_OUT_OF_MEMORY);
  NS_ADDREF(*aFormSubmission);

  return NS_OK;
}
